// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/send_as/send_as_account.h"

#include "api/api_sending.h"
#include "apiwrap.h"
#include "ayu/utils/telegram_helpers.h"
#include "core/application.h"
#include "core/version.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/session/send_as_peers.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/serialize_document.h"
#include "ui/chat/attach/attach_prepare.h"

namespace AyuSendAs {
namespace {

struct State {
	base::flat_map<std::pair<UserId, PeerId>, UserId> chosen;
	rpl::event_stream<not_null<PeerData*>> changes;
};

State &state() {
	static auto result = State();
	return result;
}

PeerData *postablePeer(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	const auto result = session->data().peerLoaded(peer->id);
	return (result && Data::CanSendAnything(result, false))
		? result
		: nullptr;
}

DocumentData *documentFor(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	const auto existing = session->data().document(document->id);
	if (existing->hasRemoteLocation()) {
		return existing;
	}
	auto bytes = QByteArray();
	{
		QDataStream stream(&bytes, QIODevice::WriteOnly);
		Serialize::Document::writeToStream(stream, document);
	}
	QDataStream stream(bytes);
	if (const auto sticker = document->sticker()) {
		const auto &set = sticker->set;
		return Serialize::Document::readStickerFromStream(
			session,
			AppVersion,
			stream,
			{ set.id, set.accessHash, set.shortName });
	}
	return Serialize::Document::readFromStream(session, AppVersion, stream);
}

bool redirect(Api::SendAction &action) {
	const auto account = chosen(action.history->peer);
	if (!account) {
		return false;
	}
	const auto peer = action.history->peer;
	auto &api = action.history->session().api();
	// WHY: the open chat should react as to a normal send (scroll down, drop
	// the reply bar), and a pending forward stays on the own account,
	// because the other one may not see the forwarded messages.
	if (!action.options.scheduled) {
		api.sendAction(action);
	}
	api.finishForwarding(action);

	action.history = account->session().data().history(peer->id);
	action.options.sendAs = nullptr;
	if (!peer->isChannel() || action.replyTo.messageId.peer != peer->id) {
		action.replyTo = FullReplyTo{
			.topicRootId = action.replyTo.topicRootId,
			.monoforumPeerId = action.replyTo.monoforumPeerId,
		};
	}
	return true;
}

} // namespace

std::vector<not_null<UserData*>> accounts(not_null<PeerData*> peer) {
	auto result = std::vector<not_null<UserData*>>();
	if (!peer->isChat() && !peer->isChannel()) {
		return result;
	}
	// shortcut: needs the chat loaded there, resolve it if accounts go missing.
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto session = account->maybeSession();
		if (session
			&& session != &peer->session()
			&& postablePeer(session, peer)) {
			result.push_back(session->user());
		}
	}
	return result;
}

UserData *chosen(not_null<PeerData*> peer) {
	const auto &map = state().chosen;
	const auto i = map.find(std::pair(peer->session().userId(), peer->id));
	if (i == end(map)) {
		return nullptr;
	}
	const auto session = getSession(i->second.bare);
	return (session
		&& session != &peer->session()
		&& postablePeer(session, peer))
		? session->user().get()
		: nullptr;
}

not_null<PeerData*> resolveChosen(not_null<PeerData*> peer) {
	if (const auto account = chosen(peer)) {
		return account;
	}
	return peer->session().sendAsPeers().resolveChosen(peer);
}

void setChosen(not_null<PeerData*> peer, UserData *account) {
	const auto key = std::pair(peer->session().userId(), peer->id);
	if (account) {
		state().chosen[key] = peerToUser(account->id);
	} else {
		state().chosen.remove(key);
	}
	state().changes.fire_copy(peer);
}

rpl::producer<> changes(not_null<PeerData*> peer) {
	return rpl::merge(
		state().changes.events(
		) | rpl::filter([=](not_null<PeerData*> changed) {
			return (changed == peer);
		}) | rpl::to_empty,
		Core::App().domain().accountsChanges());
}

void sendMessage(
		Api::MessageToSend &&message,
		std::optional<MsgId> localId) {
	if (redirect(message.action)) {
		localId = std::nullopt;
	}
	auto &api = message.action.history->session().api();
	api.sendMessage(std::move(message), localId);
}

void sendFiles(
		Ui::PreparedList &&list,
		SendMediaType type,
		std::shared_ptr<SendingAlbum> album,
		Api::SendAction action) {
	redirect(action);
	action.history->session().api().sendFiles(
		std::move(list),
		type,
		std::move(album),
		action);
}

void sendFile(
		const QByteArray &fileContent,
		SendMediaType type,
		Api::SendAction action) {
	redirect(action);
	action.history->session().api().sendFile(fileContent, type, action);
}

void sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		crl::time duration,
		bool video,
		Api::SendAction action) {
	redirect(action);
	action.history->session().api().sendVoiceMessage(
		std::move(result),
		std::move(waveform),
		duration,
		video,
		action);
}

void sendExistingDocument(
		Api::MessageToSend &&message,
		not_null<DocumentData*> document,
		std::optional<MsgId> localId) {
	const auto account = chosen(message.action.history->peer);
	const auto copy = account
		? documentFor(&account->session(), document)
		: nullptr;
	if (copy && redirect(message.action)) {
		document = copy;
		localId = std::nullopt;
	}
	Api::SendExistingDocument(std::move(message), document, localId);
}

} // namespace AyuSendAs
