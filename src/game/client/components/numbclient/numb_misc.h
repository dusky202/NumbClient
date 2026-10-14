#ifndef GAME_CLIENT_COMPONENTS_NUMBCLIENT_NUMB_MISC_H
#define GAME_CLIENT_COMPONENTS_NUMBCLIENT_NUMB_MISC_H

#include <game/client/component.h>

// Small NumbClient quality-of-life features that run every frame.
class CNumbMisc : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

private:
	int m_AppliedPriority = -1;
	int m_LastTeam = 0;
	bool m_WasSpectator = false;
	bool m_HaveState = false;
	int64_t m_LastLockSent = 0;

	void ApplyProcessPriority();
	void CheckAutoTeamLock();
	void CheckSpecMoved();
	void CheckLagGuard();

	float m_AvgPing = -1.0f;
	bool m_GuardActive = false;
	int m_SavedAntiPing = 0;
	int64_t m_GuardUntil = 0;
};

#endif
