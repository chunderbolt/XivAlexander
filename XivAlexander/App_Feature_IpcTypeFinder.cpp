#include "pch.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

static const float ExtraDelay = 0.1f;

class App::Feature::IpcTypeFinder::Internals {
public:

	class SingleConnectionHandler {
	public:
		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (pMessage->Length == 0x9c ||
							pMessage->Length == 0x29c ||
							pMessage->Length == 0x4dc ||
							pMessage->Length == 0x71c ||
							pMessage->Length == 0x95c) {
							// Test ActionEffect

							const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;

							Misc::Logger::GetLogger().Format(
								LogCategory::IpcTypeFinder,
								"%p: S2C_AttackResponse: subtype=%04x length=%x actionId=%04x sequence=%04x wait=%.3f",
								conn.GetSocket(),
								pMessage->Data.IPC.SubType,
								pMessage->Length,
								actionEffect.ActionId,
								actionEffect.SourceSequence,
								actionEffect.AnimationLockDuration);
							pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

						} else if (pMessage->Length == 0x40) {
							// Two possibilities: ActorControlSelf and ActorCast

							//
							// Test ActorControlSelf
							// 
							const auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								const auto& cooldown = actorControlSelf.Cooldown;
								Misc::Logger::GetLogger().Format(
									LogCategory::IpcTypeFinder,
									"%p: S2C_ActorControlSelf: Cooldown: actionId=%04x duration=%d",
									conn.GetSocket(),
									cooldown.ActionId,
									cooldown.Duration);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

							} else if (pMessage->Data.IPC.Data.S2C_ActorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;
								Misc::Logger::GetLogger().Format(
									LogCategory::IpcTypeFinder,
									"%p: S2C_ActorControlSelf: Rollback: actionId=%04x sourceSequence=%04x",
									conn.GetSocket(),
									rollback.ActionId,
									rollback.SourceSequence);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
							}

							//
							// Test ActorCast
							//
							Misc::Logger::GetLogger().Format(
								LogCategory::IpcTypeFinder,
								"%p: S2C_ActorCast: actionId=%04x time=%.3f target=%08x",
								conn.GetSocket(),
								pMessage->Data.IPC.Data.S2C_ActorCast.ActionId,
								pMessage->Data.IPC.Data.S2C_ActorCast.CastTime,
								pMessage->Data.IPC.Data.S2C_ActorCast.TargetId);
							pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

						} else if (pMessage->Length == 0x38) {
							// Test ActorControl
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;
								Misc::Logger::GetLogger().Format(
									LogCategory::IpcTypeFinder,
									"%p: S2C_ActorControl: CancelCast: actionId=%04x",
									conn.GetSocket(),
									pMessage->Length,
									cancelCast.ActionId);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
							}
						}
					}
					if (pMessage->Length == 0x78) {
						// Test AddStatusEffect
						const auto& actorControl = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						const auto& addStatusEffect = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						std::string effects;
						for (int i = 0; i < addStatusEffect.EffectCount; ++i) {
							const auto& entry = addStatusEffect.Effects[i];
							effects += Utils::FormatString(
								"\n\teffectId=%04x duration=%f sourceActorId=%08x",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						Misc::Logger::GetLogger().Format(
							LogCategory::IpcTypeFinder,
							"%p: S2C_AddStatusEffect: relatedActionSequence=%08x actorId=%08x HP=%d/%d MP=%d shield=%d%s",
							conn.GetSocket(),
							addStatusEffect.RelatedActionSequence,
							addStatusEffect.ActorId,
							addStatusEffect.CurrentHp,
							addStatusEffect.MaxHp,
							addStatusEffect.CurentMp,
							addStatusEffect.DamageShield,
							effects.c_str()
						);
					}
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->Length == 0x40) {
						// Test ActionRequest
						const auto& actionRequest = pMessage->Data.IPC.Data.C2S_ActionRequest;
						Misc::Logger::GetLogger().Format(
							LogCategory::IpcTypeFinder,
							"5p: C2S_AttackRequest: actionId=%04x sequence=%04x",
							conn.GetSocket(),
							actionRequest.ActionId, actionRequest.Sequence);
						pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
					}
				}
				return true;
				});
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
		}
	};

	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;

	Internals() {
		Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
		});
		Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
			});
	}

	~Internals() {
		m_handlers.clear();
		Network::SocketHook::Instance()->RemoveListeners(this);
	}
};

App::Feature::IpcTypeFinder::IpcTypeFinder()
: impl(std::make_unique<Internals>()){
}

App::Feature::IpcTypeFinder::~IpcTypeFinder() {
}
