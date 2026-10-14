#include "numb_misc.h"

#include <base/time.h>

#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void CNumbMisc::OnReset()
{
	m_HaveState = false;
	m_LastTeam = 0;
	m_WasSpectator = false;
}

void CNumbMisc::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		m_HaveState = false;
}

void CNumbMisc::ApplyProcessPriority()
{
	if(m_AppliedPriority == g_Config.m_NcProcessPriority)
		return;
	m_AppliedPriority = g_Config.m_NcProcessPriority;
#if defined(CONF_FAMILY_WINDOWS)
	DWORD PriorityClass = NORMAL_PRIORITY_CLASS;
	if(m_AppliedPriority == 1)
		PriorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
	else if(m_AppliedPriority == 2)
		PriorityClass = HIGH_PRIORITY_CLASS;
	SetPriorityClass(GetCurrentProcess(), PriorityClass);
#endif
}

void CNumbMisc::CheckAutoTeamLock()
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;
	const int Team = GameClient()->m_Teams.Team(LocalId);
	if(m_HaveState && g_Config.m_NcAutoTeamLock && Team != m_LastTeam && Team > 0 && Team < TEAM_SUPER)
	{
		// avoid spamming the server if team info flickers
		const int64_t Now = time_get();
		if(Now - m_LastLockSent > time_freq() * 3)
		{
			GameClient()->m_Chat.SendChat(0, "/lock 1");
			m_LastLockSent = Now;
		}
	}
	m_LastTeam = Team;
}

void CNumbMisc::CheckSpecMoved()
{
	const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_pLocalInfo;
	if(!pInfo)
		return;
	const bool IsSpectator = pInfo->m_Team == TEAM_SPECTATORS;
	if(m_HaveState && g_Config.m_NcSpecMovedNotify && IsSpectator && !m_WasSpectator)
	{
		GameClient()->m_Chat.Echo(TCLocalize("You were moved to the spectators!"));
		GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
	}
	m_WasSpectator = IsSpectator;
}

void CNumbMisc::OnRender()
{
	ApplyProcessPriority();

	if(Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalInfo)
		return;

	CheckAutoTeamLock();
	CheckSpecMoved();
	m_HaveState = true;
}
