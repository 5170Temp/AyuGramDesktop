// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "api/api_common.h"
#include "data/data_types.h"

class DocumentData;
class PeerData;
class UserData;
struct SendingAlbum;
enum class SendMediaType;

namespace Ui {
struct PreparedList;
} // namespace Ui

namespace AyuSendAs {

[[nodiscard]] std::vector<not_null<UserData*>> accounts(
	not_null<PeerData*> peer);
[[nodiscard]] UserData *chosen(not_null<PeerData*> peer);
[[nodiscard]] not_null<PeerData*> resolveChosen(not_null<PeerData*> peer);
void setChosen(not_null<PeerData*> peer, UserData *account);
[[nodiscard]] rpl::producer<> changes(not_null<PeerData*> peer);

void sendMessage(
	Api::MessageToSend &&message,
	std::optional<MsgId> localId = std::nullopt);
void sendFiles(
	Ui::PreparedList &&list,
	SendMediaType type,
	std::shared_ptr<SendingAlbum> album,
	Api::SendAction action);
void sendFile(
	const QByteArray &fileContent,
	SendMediaType type,
	Api::SendAction action);
void sendVoiceMessage(
	QByteArray result,
	VoiceWaveform waveform,
	crl::time duration,
	bool video,
	Api::SendAction action);
void sendExistingDocument(
	Api::MessageToSend &&message,
	not_null<DocumentData*> document,
	std::optional<MsgId> localId = std::nullopt);

} // namespace AyuSendAs
