#include "numb_hud.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/image.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>
#include <game/localization.h>

#include <cmath>
#include <deque>

static ColorRGBA ConfigColor(unsigned Value)
{
	return color_cast<ColorRGBA>(ColorHSLA(Value, true));
}

// ---------------------------------------------------------------------------
// CNumbWorld
// ---------------------------------------------------------------------------

void CNumbWorld::RenderHitbox(vec2 Pos)
{
	const float Half = CCharacterCore::PhysicalSize() / 2.0f;
	const ColorRGBA Col = ConfigColor(g_Config.m_NcHitboxColor);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Col);
	const IGraphics::CLineItem aLines[] = {
		IGraphics::CLineItem(Pos.x - Half, Pos.y - Half, Pos.x + Half, Pos.y - Half),
		IGraphics::CLineItem(Pos.x + Half, Pos.y - Half, Pos.x + Half, Pos.y + Half),
		IGraphics::CLineItem(Pos.x + Half, Pos.y + Half, Pos.x - Half, Pos.y + Half),
		IGraphics::CLineItem(Pos.x - Half, Pos.y + Half, Pos.x - Half, Pos.y - Half),
	};
	Graphics()->LinesDraw(aLines, std::size(aLines));
	Graphics()->LinesEnd();

	// faint fill so the box is visible on busy maps
	Graphics()->DrawRect(Pos.x - Half, Pos.y - Half, Half * 2.0f, Half * 2.0f, Col.WithMultipliedAlpha(0.15f), IGraphics::CORNER_NONE, 0.0f);
}

void CNumbWorld::RenderNinjaTimer(int ClientId, vec2 Pos)
{
	const CCharacterCore &Core = GameClient()->m_aClients[ClientId].m_Predicted;
	if(!Core.m_aWeapons[WEAPON_NINJA].m_Got || Core.m_ActiveWeapon != WEAPON_NINJA)
		return;

	const int TickSpeed = Client()->GameTickSpeed();
	const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * TickSpeed / 1000;
	if(Max <= 0)
		return;
	const int Left = std::clamp(Core.m_Ninja.m_ActivationTick + Max - Client()->GameTick(g_Config.m_ClDummy), 0, Max);
	if(Left <= 0)
		return;
	const float Progress = Left / (float)Max;

	const float Width = 64.0f;
	const float Height = 6.0f;
	const float x = Pos.x - Width / 2.0f;
	const float y = Pos.y - 58.0f;

	Graphics()->DrawRect(x - 1.0f, y - 1.0f, Width + 2.0f, Height + 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 2.0f);
	const ColorRGBA Fill(1.0f + (0.55f - 1.0f) * Progress, 0.25f + (0.85f - 0.25f) * Progress, 0.25f + (1.0f - 0.25f) * Progress, 0.95f);
	Graphics()->DrawRect(x, y, Width * Progress, Height, Fill, IGraphics::CORNER_ALL, 2.0f);

	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%.1f", Left / (float)TickSpeed);
	const float FontSize = 9.0f;
	const float TextW = TextRender()->TextWidth(FontSize, aBuf);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->Text(Pos.x - TextW / 2.0f, y - FontSize - 2.0f, FontSize, aBuf);
}

void CNumbWorld::DrawCircleOutline(vec2 Center, float Radius, ColorRGBA Color)
{
	constexpr int Segments = 48;
	IGraphics::CLineItem aLines[Segments];
	for(int i = 0; i < Segments; i++)
	{
		const float a0 = i / (float)Segments * 2.0f * pi;
		const float a1 = (i + 1) / (float)Segments * 2.0f * pi;
		aLines[i] = IGraphics::CLineItem(Center + direction(a0) * Radius, Center + direction(a1) * Radius);
	}
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, Segments);
	Graphics()->LinesEnd();
}

void CNumbWorld::DrawArrow(vec2 From, vec2 To, ColorRGBA Color)
{
	const vec2 Dir = normalize(To - From);
	const vec2 Side = vec2(-Dir.y, Dir.x);
	const float Head = 9.0f;
	const IGraphics::CLineItem aLines[] = {
		IGraphics::CLineItem(From, To),
		IGraphics::CLineItem(To, To - Dir * Head + Side * Head * 0.6f),
		IGraphics::CLineItem(To, To - Dir * Head - Side * Head * 0.6f),
	};
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	Graphics()->LinesDraw(aLines, std::size(aLines));
	Graphics()->LinesEnd();
}

// Mirrors the hammer logic of the game (see CCharacter::FireWeapon, WEAPON_HAMMER):
// the hit zone is centered 0.75 tee-sizes in front of you along your aim, and a hit tee
// is pushed along normalize(normalize(TargetPos - YourPos) + (0, -1.1)).
// This only visualizes the result of your own aim, it never changes input.
void CNumbWorld::RenderHammerIndicator()
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active || Client()->State() != IClient::STATE_ONLINE)
		return;
	const CCharacterCore &LocalCore = GameClient()->m_aClients[LocalId].m_Predicted;
	if(LocalCore.m_ActiveWeapon != WEAPON_HAMMER || LocalCore.m_HammerHitDisabled)
		return;

	const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	vec2 Aim = vec2(Input.m_TargetX, Input.m_TargetY);
	if(length(Aim) < 0.0001f)
		return;
	Aim = normalize(Aim);

	const float Proximity = CCharacterCore::PhysicalSize();
	const vec2 LocalPos = GameClient()->m_aClients[LocalId].m_RenderPos;
	const vec2 HitCenter = LocalPos + Aim * Proximity * 0.75f;
	const float HitRadius = Proximity * 0.5f + Proximity; // search radius + target proximity radius

	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);
	bool AnyHit = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == LocalId || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const vec2 TargetPos = GameClient()->m_aClients[ClientId].m_RenderPos;
		if(distance(TargetPos, HitCenter) >= HitRadius)
			continue;
		AnyHit = true;

		vec2 Dir = length(TargetPos - LocalPos) > 0.0f ? normalize(TargetPos - LocalPos) : vec2(0.0f, -1.0f);
		const vec2 Push = normalize(Dir + vec2(0.0f, -1.1f));
		DrawCircleOutline(TargetPos, Proximity * 0.5f + 2.0f, ColorRGBA(1.0f, 0.85f, 0.3f, 0.9f));
		DrawArrow(TargetPos, TargetPos + Push * 70.0f, ColorRGBA(1.0f, 0.85f, 0.3f, 0.95f));
	}
	DrawCircleOutline(HitCenter, HitRadius, Accent.WithAlpha(AnyHit ? 0.8f : 0.35f));
}

void CNumbWorld::RenderFog(const CScreenRect &ScreenRect)
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float R = (float)g_Config.m_NcFogRadius;
	const ColorRGBA Fog(0.0f, 0.0f, 0.0f, g_Config.m_NcFogAlpha / 100.0f);
	// far enough to always cover the visible area
	const vec2 Size = ScreenRect.m_BottomRight - ScreenRect.m_TopLeft;
	const float Far = std::max(Size.x, Size.y) * 2.0f + R;

	Graphics()->TextureClear();
	if(g_Config.m_NcFog == 2)
	{
		// rectangle: four slabs around the clear area
		Graphics()->DrawRect(Center.x - Far, Center.y - Far, Far * 2.0f, Far - R, Fog, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect(Center.x - Far, Center.y + R, Far * 2.0f, Far - R, Fog, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect(Center.x - Far, Center.y - R, Far - R, R * 2.0f, Fog, IGraphics::CORNER_NONE, 0.0f);
		Graphics()->DrawRect(Center.x + R, Center.y - R, Far - R, R * 2.0f, Fog, IGraphics::CORNER_NONE, 0.0f);
		return;
	}

	// circle: a ring from R to Far, with a short soft edge
	constexpr int Segments = 64;
	const float Soft = R * 0.15f;
	Graphics()->QuadsBegin();
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const float Inner = Pass == 0 ? R : R - Soft;
		const float Outer = Pass == 0 ? Far : R;
		IGraphics::CFreeformItem aItems[Segments];
		for(int i = 0; i < Segments; i++)
		{
			const vec2 d0 = direction(i / (float)Segments * 2.0f * pi);
			const vec2 d1 = direction((i + 1) / (float)Segments * 2.0f * pi);
			aItems[i] = IGraphics::CFreeformItem(Center + d0 * Inner, Center + d1 * Inner, Center + d0 * Outer, Center + d1 * Outer);
		}
		if(Pass == 0)
			Graphics()->SetColor(Fog);
		else
			Graphics()->SetColor4(Fog.WithAlpha(0.0f), Fog.WithAlpha(0.0f), Fog, Fog);
		Graphics()->QuadsDrawFreeform(aItems, Segments);
	}
	Graphics()->QuadsEnd();
}

void CNumbWorld::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_NcShowHitbox && !g_Config.m_NcNinjaTimer && !g_Config.m_NcHammerIndicator && !g_Config.m_NcFog)
		return;

	// make sure we draw in world coordinates
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const CScreenRect ScreenRect = Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
	Graphics()->MapScreen(ScreenRect);

	if(g_Config.m_NcFog)
		RenderFog(ScreenRect);

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const vec2 Pos = GameClient()->m_aClients[ClientId].m_RenderPos;
		if(!ScreenRect.Inside(Pos))
			continue;

		const bool IsLocal = ClientId == LocalId;
		if(g_Config.m_NcShowHitbox == 2 || (g_Config.m_NcShowHitbox == 1 && IsLocal))
			RenderHitbox(Pos);
		if(g_Config.m_NcNinjaTimer)
			RenderNinjaTimer(ClientId, Pos);
	}
	if(g_Config.m_NcHammerIndicator)
		RenderHammerIndicator();
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// CNumbHud
// ---------------------------------------------------------------------------

void CNumbHud::ConSessionReset(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CNumbHud *>(pUserData)->ResetSession();
}

void CNumbHud::OnConsoleInit()
{
	Console()->Register("nc_session_reset", "", CFGFLAG_CLIENT, ConSessionReset, this, "Reset NumbClient session statistics");
	Console()->Register("nc_hud_editor", "?i[on]", CFGFLAG_CLIENT, ConHudEditor, this, "Open the NumbClient HUD editor (no argument toggles it)");
}

void CNumbHud::OnInit()
{
	ResetSession();
}

void CNumbHud::ResetSession()
{
	m_SessionStart = time_get();
	m_SessionDeaths = 0;
	m_SessionFinishes = 0;
	m_SessionKills = 0;
}

void CNumbHud::OnMessage(int MsgType, void *pRawMsg)
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		const CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim == LocalId)
			m_SessionDeaths++;
		else if(pMsg->m_Killer == LocalId)
			m_SessionKills++;
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(pMsg->m_ClientId == LocalId)
			m_SessionFinishes++;
	}
}

bool CNumbHud::ShouldRender() const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return false;
	if(!g_Config.m_ClShowhud)
		return false;
	if(GameClient()->m_Scoreboard.IsActive())
		return false;
	return true;
}

void CNumbHud::RenderPanel(float x, float y, float w, float h)
{
	Graphics()->DrawRect(x, y, w, h, ColorRGBA(0.06f, 0.05f, 0.09f, 0.55f), IGraphics::CORNER_ALL, 3.0f);
	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);
	Graphics()->DrawRect(x, y, 2.0f, h, Accent.WithMultipliedAlpha(0.9f), IGraphics::CORNER_L, 3.0f);
}

void CNumbHud::RenderKeyIndicator()
{
	if(!GameClient()->m_Snap.m_pLocalCharacter && !m_EditorActive)
		return;

	const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	const float Scale = g_Config.m_NcKeyIndicatorSize / 100.0f;
	const float Key = 14.0f * Scale;
	const float Gap = 2.0f * Scale;
	const float FontSize = 5.5f * Scale;
	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);

	struct SKey
	{
		const char *m_pLabel;
		bool m_Active;
		float m_Col;
		float m_Row;
		float m_Width;
	};
	const SKey aKeys[] = {
		{TCLocalize("JUMP"), Input.m_Jump != 0, 0.0f, 0.0f, 3.0f},
		{"\xe2\x86\x90", Input.m_Direction < 0, 0.0f, 1.0f, 1.0f}, // ←
		{"\xe2\x86\x93", false, 1.0f, 1.0f, 1.0f}, // placeholder (no down key in teeworlds), drawn dim
		{"\xe2\x86\x92", Input.m_Direction > 0, 2.0f, 1.0f, 1.0f}, // →
		{TCLocalize("HOOK"), Input.m_Hook != 0, 0.0f, 2.0f, 1.5f},
		{TCLocalize("FIRE"), (Input.m_Fire & 1) != 0, 1.5f, 2.0f, 1.5f},
	};

	const float TotalW = Key * 3.0f + Gap * 2.0f;
	const float TotalH = Key * 3.0f + Gap * 2.0f;
	const float Pad = 3.0f * Scale;
	float x = m_Width * g_Config.m_NcKeyIndicatorX / 1000.0f - TotalW / 2.0f;
	float y = m_Height * g_Config.m_NcKeyIndicatorY / 1000.0f - TotalH / 2.0f;
	x = std::clamp(x, Pad, m_Width - TotalW - Pad);
	y = std::clamp(y, Pad, m_Height - TotalH - Pad);

	RenderPanel(x - Pad, y - Pad, TotalW + Pad * 2.0f, TotalH + Pad * 2.0f);
	StoreRect(WIDGET_KEYS, x - Pad, y - Pad, TotalW + Pad * 2.0f, TotalH + Pad * 2.0f);

	for(const SKey &K : aKeys)
	{
		const bool Placeholder = K.m_Col == 1.0f && K.m_Row == 1.0f;
		const float kx = x + K.m_Col * (Key + Gap);
		const float ky = y + K.m_Row * (Key + Gap);
		const float kw = K.m_Width * Key + (K.m_Width - 1.0f) * Gap;
		ColorRGBA Bg = K.m_Active ? Accent.WithAlpha(0.9f) : ColorRGBA(1.0f, 1.0f, 1.0f, Placeholder ? 0.04f : 0.10f);
		Graphics()->DrawRect(kx, ky, kw, Key, Bg, IGraphics::CORNER_ALL, 2.5f * Scale);
		if(Placeholder)
			continue;
		const float TextW = TextRender()->TextWidth(FontSize, K.m_pLabel);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, K.m_Active ? 1.0f : 0.6f);
		TextRender()->Text(kx + (kw - TextW) / 2.0f, ky + (Key - FontSize) / 2.0f - FontSize * 0.1f, FontSize, K.m_pLabel);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CNumbHud::RenderInputVisualizer()
{
	if(!GameClient()->m_Snap.m_pLocalCharacter && !m_EditorActive)
		return;

	const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	const float Scale = g_Config.m_NcInputVisualizerSize / 100.0f;
	const float Radius = 18.0f * Scale;
	const float Pad = 4.0f * Scale;
	const float FontSize = 5.5f * Scale;
	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);

	float cx = m_Width * g_Config.m_NcInputVisualizerX / 1000.0f;
	float cy = m_Height * g_Config.m_NcInputVisualizerY / 1000.0f;
	cx = std::clamp(cx, Radius + Pad, m_Width - Radius - Pad);
	cy = std::clamp(cy, Radius + Pad, m_Height - Radius - Pad - FontSize * 1.5f);

	RenderPanel(cx - Radius - Pad, cy - Radius - Pad, (Radius + Pad) * 2.0f, (Radius + Pad) * 2.0f + FontSize * 1.5f);
	StoreRect(WIDGET_INPUT, cx - Radius - Pad, cy - Radius - Pad, (Radius + Pad) * 2.0f, (Radius + Pad) * 2.0f + FontSize * 1.5f);

	// dial
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.08f);
	Graphics()->DrawCircle(cx, cy, Radius, 48);
	Graphics()->QuadsEnd();

	vec2 Dir = vec2(Input.m_TargetX, Input.m_TargetY);
	float Angle = 0.0f;
	if(length(Dir) > 0.0001f)
	{
		Dir = normalize(Dir);
		Angle = std::atan2(-Dir.y, Dir.x) * 180.0f / pi;
		if(Angle < 0.0f)
			Angle += 360.0f;
	}
	else
		Dir = vec2(1.0f, 0.0f);

	const vec2 Tip = vec2(cx, cy) + Dir * (Radius - 2.0f * Scale);
	Graphics()->LinesBegin();
	Graphics()->SetColor(Accent.WithAlpha(1.0f));
	const IGraphics::CLineItem Line(cx, cy, Tip.x, Tip.y);
	Graphics()->LinesDraw(&Line, 1);
	Graphics()->LinesEnd();

	Graphics()->QuadsBegin();
	Graphics()->SetColor(Accent.WithAlpha(1.0f));
	Graphics()->DrawCircle(Tip.x, Tip.y, 2.0f * Scale, 16);
	// direction / jump / hook dots
	if(Input.m_Direction != 0)
	{
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.9f);
		Graphics()->DrawCircle(cx + Input.m_Direction * (Radius + Pad * 0.5f), cy, 1.5f * Scale, 12);
	}
	if(Input.m_Jump)
	{
		Graphics()->SetColor(0.5f, 0.9f, 1.0f, 0.9f);
		Graphics()->DrawCircle(cx, cy - Radius - Pad * 0.5f, 1.5f * Scale, 12);
	}
	if(Input.m_Hook)
	{
		Graphics()->SetColor(1.0f, 0.8f, 0.3f, 0.9f);
		Graphics()->DrawCircle(cx, cy, 2.0f * Scale, 12);
	}
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.1f\xc2\xb0", Angle);
	const float TextW = TextRender()->TextWidth(FontSize, aBuf);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.85f);
	TextRender()->Text(cx - TextW / 2.0f, cy + Radius + Pad * 0.5f, FontSize, aBuf);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CNumbHud::RenderSessionStats()
{
	const float FontSize = 6.0f;
	const float LineH = FontSize + 2.0f;
	const float Pad = 4.0f;

	const int64_t Seconds = (time_get() - m_SessionStart) / time_freq();
	char aTime[32];
	str_format(aTime, sizeof(aTime), "%02d:%02d:%02d", (int)(Seconds / 3600), (int)((Seconds / 60) % 60), (int)(Seconds % 60));

	char aaLines[4][64];
	str_format(aaLines[0], sizeof(aaLines[0]), "%s: %s", TCLocalize("Playtime"), aTime);
	str_format(aaLines[1], sizeof(aaLines[1]), "%s: %d", TCLocalize("Deaths"), m_SessionDeaths);
	str_format(aaLines[2], sizeof(aaLines[2]), "%s: %d", TCLocalize("Finishes"), m_SessionFinishes);
	str_format(aaLines[3], sizeof(aaLines[3]), "%s: %d", TCLocalize("Kills"), m_SessionKills);

	float MaxW = TextRender()->TextWidth(FontSize, TCLocalize("Session"));
	for(const auto &aLine : aaLines)
		MaxW = std::max(MaxW, TextRender()->TextWidth(FontSize, aLine));

	const float w = MaxW + Pad * 2.0f + 2.0f;
	const float h = LineH * 5.0f + Pad * 2.0f;
	float x = std::clamp(m_Width * g_Config.m_NcSessionStatsX / 1000.0f, 0.0f, m_Width - w);
	float y = std::clamp(m_Height * g_Config.m_NcSessionStatsY / 1000.0f, 0.0f, m_Height - h);

	RenderPanel(x, y, w, h);
	StoreRect(WIDGET_SESSION, x, y, w, h);
	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);
	TextRender()->TextColor(Accent.WithAlpha(1.0f));
	TextRender()->Text(x + Pad + 2.0f, y + Pad, FontSize, TCLocalize("Session"));
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
	for(int i = 0; i < 4; i++)
		TextRender()->Text(x + Pad + 2.0f, y + Pad + LineH * (i + 1), FontSize, aaLines[i]);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CNumbHud::RenderTeamIndicator()
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || (!GameClient()->m_Snap.m_pLocalCharacter && !m_EditorActive))
		return;
	const int Team = GameClient()->m_Teams.Team(LocalId);
	if(Team == 0 && !m_EditorActive) // only meaningful inside a team
		return;

	int Total = 0;
	int Frozen = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[ClientId] || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		if(Data.m_Spec || GameClient()->m_Teams.Team(ClientId) != Team)
			continue;
		Total++;
		if(Data.m_FreezeEnd > 0 || Data.m_DeepFrozen)
			Frozen++;
	}
	if(m_EditorActive && Total <= 1)
		Total = 2; // preview
	if(Total <= 1)
		return;

	const CGameClient::CClientData &Local = GameClient()->m_aClients[LocalId];
	const bool LocalFrozen = Local.m_FreezeEnd > 0 || Local.m_DeepFrozen;
	const bool LastStanding = !LocalFrozen && Frozen == Total - 1;

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), TCLocalize("Frozen: %d/%d"), Frozen, Total);
	const char *pWarn = LastStanding ? TCLocalize("LAST ONE STANDING!") : nullptr;

	const float FontSize = 7.0f;
	const float Pad = 4.0f;
	float w = TextRender()->TextWidth(FontSize, aBuf);
	if(pWarn)
		w = std::max(w, TextRender()->TextWidth(FontSize, pWarn));
	w += Pad * 2.0f + 2.0f;
	const float h = FontSize * (pWarn ? 2.0f : 1.0f) + 2.0f + Pad * 2.0f;
	float x = std::clamp(m_Width * g_Config.m_NcTeamIndicatorX / 1000.0f - w / 2.0f, 0.0f, m_Width - w);
	float y = std::clamp(m_Height * g_Config.m_NcTeamIndicatorY / 1000.0f - h / 2.0f, 0.0f, m_Height - h);

	RenderPanel(x, y, w, h);
	StoreRect(WIDGET_TEAM, x, y, w, h);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	TextRender()->Text(x + Pad + 2.0f, y + Pad, FontSize, aBuf);
	if(pWarn)
	{
		const float Blink = 0.6f + 0.4f * std::sin(Client()->LocalTime() * 8.0f);
		TextRender()->TextColor(1.0f, 0.3f, 0.3f, Blink);
		TextRender()->Text(x + Pad + 2.0f, y + Pad + FontSize + 2.0f, FontSize, pWarn);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CNumbHud::OnRender()
{
	if(m_EditorActive && (Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive()))
		SetEditorActive(false);
	if(!ShouldRender())
		return;
	if(!m_EditorActive && !g_Config.m_NcKeyIndicator && !g_Config.m_NcInputVisualizer && !g_Config.m_NcSessionStats && !g_Config.m_NcTeamIndicator && !g_Config.m_NcMapProgress)
		return;
	for(auto &aRect : m_aWidgetRect)
		aRect[2] = 0.0f;

	m_Height = 300.0f;
	m_Width = m_Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(m_Width, m_Height);

	if(g_Config.m_NcKeyIndicator)
		RenderKeyIndicator();
	if(g_Config.m_NcInputVisualizer)
		RenderInputVisualizer();
	if(g_Config.m_NcSessionStats)
		RenderSessionStats();
	if(g_Config.m_NcTeamIndicator)
		RenderTeamIndicator();
	if(g_Config.m_NcMapProgress)
		RenderMapProgress();

	if(m_EditorActive)
		RenderEditor();
}

// ---------------------------------------------------------------------------
// HUD editor
// ---------------------------------------------------------------------------

void CNumbHud::ConHudEditor(IConsole::IResult *pResult, void *pUserData)
{
	CNumbHud *pThis = static_cast<CNumbHud *>(pUserData);
	if(pResult->NumArguments() > 0)
		pThis->SetEditorActive(pResult->GetInteger(0) != 0);
	else
		pThis->SetEditorActive(!pThis->m_EditorActive);
}

void CNumbHud::SetEditorActive(bool Active)
{
	if(Active && Client()->State() != IClient::STATE_ONLINE)
		return;
	m_EditorActive = Active;
	m_DragWidget = -1;
	if(Active)
	{
		m_Height = 300.0f;
		m_Width = m_Height * Graphics()->ScreenAspect();
		m_EditorMouse = vec2(m_Width / 2.0f, m_Height / 2.0f);
	}
}

void CNumbHud::OnRelease()
{
	m_DragWidget = -1;
}

int *CNumbHud::WidgetConfigX(int Widget)
{
	switch(Widget)
	{
	case WIDGET_KEYS: return &g_Config.m_NcKeyIndicatorX;
	case WIDGET_INPUT: return &g_Config.m_NcInputVisualizerX;
	case WIDGET_SESSION: return &g_Config.m_NcSessionStatsX;
	case WIDGET_TEAM: return &g_Config.m_NcTeamIndicatorX;
	case WIDGET_PROGRESS: return &g_Config.m_NcMapProgressX;
	}
	return nullptr;
}

int *CNumbHud::WidgetConfigY(int Widget)
{
	switch(Widget)
	{
	case WIDGET_KEYS: return &g_Config.m_NcKeyIndicatorY;
	case WIDGET_INPUT: return &g_Config.m_NcInputVisualizerY;
	case WIDGET_SESSION: return &g_Config.m_NcSessionStatsY;
	case WIDGET_TEAM: return &g_Config.m_NcTeamIndicatorY;
	case WIDGET_PROGRESS: return &g_Config.m_NcMapProgressY;
	}
	return nullptr;
}

void CNumbHud::StoreRect(int Widget, float x, float y, float w, float h)
{
	m_aWidgetRect[Widget][0] = x;
	m_aWidgetRect[Widget][1] = y;
	m_aWidgetRect[Widget][2] = w;
	m_aWidgetRect[Widget][3] = h;
}

bool CNumbHud::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_EditorActive)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 Delta = vec2(x * m_Width / pScreen->w, y * m_Height / pScreen->h);
	const vec2 OldMouse = m_EditorMouse;
	m_EditorMouse.x = std::clamp(m_EditorMouse.x + Delta.x, 0.0f, m_Width);
	m_EditorMouse.y = std::clamp(m_EditorMouse.y + Delta.y, 0.0f, m_Height);

	if(m_DragWidget >= 0 && m_Width > 0.0f)
	{
		const vec2 Moved = m_EditorMouse - OldMouse;
		m_aDragPos[m_DragWidget].x = std::clamp(m_aDragPos[m_DragWidget].x + Moved.x * 1000.0f / m_Width, 0.0f, 1000.0f);
		m_aDragPos[m_DragWidget].y = std::clamp(m_aDragPos[m_DragWidget].y + Moved.y * 1000.0f / m_Height, 0.0f, 1000.0f);
		*WidgetConfigX(m_DragWidget) = round_to_int(m_aDragPos[m_DragWidget].x);
		*WidgetConfigY(m_DragWidget) = round_to_int(m_aDragPos[m_DragWidget].y);
	}
	return true;
}

bool CNumbHud::OnInput(const IInput::CEvent &Event)
{
	if(!m_EditorActive)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		SetEditorActive(false);
		return true;
	}

	if(Event.m_Key == KEY_MOUSE_1)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_DragWidget = -1;
			for(int i = NUM_WIDGETS - 1; i >= 0; i--)
			{
				const float *r = m_aWidgetRect[i];
				if(r[2] > 0.0f && m_EditorMouse.x >= r[0] && m_EditorMouse.x <= r[0] + r[2] && m_EditorMouse.y >= r[1] && m_EditorMouse.y <= r[1] + r[3])
				{
					m_DragWidget = i;
					m_aDragPos[i] = vec2(*WidgetConfigX(i), *WidgetConfigY(i));
					break;
				}
			}
		}
		if(Event.m_Flags & IInput::FLAG_RELEASE)
			m_DragWidget = -1;
		return true;
	}
	if(Event.m_Key == KEY_MOUSE_2 && Event.m_Flags & IInput::FLAG_PRESS)
		return true;

	return false;
}

void CNumbHud::RenderEditor()
{
	const ColorRGBA Accent = ConfigColor(g_Config.m_NcHudAccentColor);
	static const char *s_apNames[NUM_WIDGETS] = {"Keys", "Input", "Session", "Team", "Progress"};

	// dim the world a bit so widgets stand out
	Graphics()->DrawRect(0.0f, 0.0f, m_Width, m_Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_NONE, 0.0f);

	for(int i = 0; i < NUM_WIDGETS; i++)
	{
		const float *r = m_aWidgetRect[i];
		if(r[2] <= 0.0f)
			continue;
		const bool Hot = m_DragWidget == i || (m_EditorMouse.x >= r[0] && m_EditorMouse.x <= r[0] + r[2] && m_EditorMouse.y >= r[1] && m_EditorMouse.y <= r[1] + r[3]);
		const ColorRGBA Outline = Accent.WithAlpha(Hot ? 1.0f : 0.6f);
		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(Outline);
		const IGraphics::CLineItem aLines[] = {
			IGraphics::CLineItem(r[0], r[1], r[0] + r[2], r[1]),
			IGraphics::CLineItem(r[0] + r[2], r[1], r[0] + r[2], r[1] + r[3]),
			IGraphics::CLineItem(r[0] + r[2], r[1] + r[3], r[0], r[1] + r[3]),
			IGraphics::CLineItem(r[0], r[1] + r[3], r[0], r[1]),
		};
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
		TextRender()->TextColor(Outline);
		TextRender()->Text(r[0], r[1] - 6.0f, 5.0f, s_apNames[i]);
	}

	const char *pHint = TCLocalize("HUD editor: drag widgets with the mouse, ESC to close");
	const float HintSize = 6.0f;
	const float HintW = TextRender()->TextWidth(HintSize, pHint);
	Graphics()->DrawRect(m_Width / 2.0f - HintW / 2.0f - 5.0f, 4.0f, HintW + 10.0f, HintSize + 6.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 3.0f);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->Text(m_Width / 2.0f - HintW / 2.0f, 7.0f, HintSize, pHint);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	RenderTools()->RenderCursor(m_EditorMouse, 12.0f);
}

// ---------------------------------------------------------------------------
// Map progress
// ---------------------------------------------------------------------------

void CNumbHud::OnMapLoad()
{
	m_vFinishDist.clear();
	m_StartDist = -1;
	m_LastProgress = 0.0f;

	const CCollision *pCol = Collision();
	m_MapWidth = pCol->GetWidth();
	m_MapHeight = pCol->GetHeight();
	if(m_MapWidth <= 0 || m_MapHeight <= 0 || !pCol->GameLayer())
		return;

	const int Num = m_MapWidth * m_MapHeight;
	m_vFinishDist.assign(Num, -1);
	std::vector<bool> vSolid(Num, false);
	std::deque<int> Queue;
	std::vector<int> vStarts;
	for(int i = 0; i < Num; i++)
	{
		const int Game = pCol->GetTileIndex(i);
		const int Front = pCol->GetFrontTileIndex(i);
		vSolid[i] = Game == TILE_SOLID || Game == TILE_NOHOOK;
		if(Game == TILE_FINISH || Front == TILE_FINISH)
		{
			m_vFinishDist[i] = 0;
			Queue.push_back(i);
		}
		if(Game == TILE_START || Front == TILE_START)
			vStarts.push_back(i);
	}
	if(Queue.empty())
	{
		m_vFinishDist.clear();
		return;
	}

	while(!Queue.empty())
	{
		const int i = Queue.front();
		Queue.pop_front();
		const int x = i % m_MapWidth;
		const int y = i / m_MapWidth;
		const int aNeighbours[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
		for(const auto &aN : aNeighbours)
		{
			if(aN[0] < 0 || aN[1] < 0 || aN[0] >= m_MapWidth || aN[1] >= m_MapHeight)
				continue;
			const int n = aN[1] * m_MapWidth + aN[0];
			if(vSolid[n] || m_vFinishDist[n] != -1)
				continue;
			m_vFinishDist[n] = m_vFinishDist[i] + 1;
			Queue.push_back(n);
		}
	}

	for(int i : vStarts)
		if(m_vFinishDist[i] > m_StartDist)
			m_StartDist = m_vFinishDist[i];
}

void CNumbHud::RenderMapProgress()
{
	if(m_vFinishDist.empty() || m_StartDist <= 0)
	{
		if(!m_EditorActive)
			return;
	}

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	float Progress = m_LastProgress;
	if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active && !m_vFinishDist.empty() && m_StartDist > 0)
	{
		const vec2 Pos = GameClient()->m_aClients[LocalId].m_RenderPos;
		const int tx = std::clamp((int)(Pos.x / 32.0f), 0, m_MapWidth - 1);
		const int ty = std::clamp((int)(Pos.y / 32.0f), 0, m_MapHeight - 1);
		const int Dist = m_vFinishDist[ty * m_MapWidth + tx];
		if(Dist >= 0)
			Progress = std::clamp(1.0f - Dist / (float)m_StartDist, 0.0f, 1.0f);
		m_LastProgress = Progress;
	}

	char aBuf[96];
	const int RaceTime = GameClient()->CurrentRaceTime();
	if(GameClient()->LastRaceTick() >= 0 && Progress > 0.05f && RaceTime > 0)
	{
		const int Eta = (int)(RaceTime / Progress);
		str_format(aBuf, sizeof(aBuf), "%s %d%%  \xc2\xb7  %s %02d:%02d", TCLocalize("Progress"), (int)(Progress * 100.0f), TCLocalize("Est. finish"), Eta / 60, Eta % 60);
	}
	else
		str_format(aBuf, sizeof(aBuf), "%s %d%%", TCLocalize("Progress"), (int)(Progress * 100.0f));

	const float FontSize = 6.0f;
	const float Pad = 4.0f;
	const float BarH = 3.0f;
	const float w = std::max(110.0f, TextRender()->TextWidth(FontSize, aBuf) + Pad * 2.0f + 2.0f);
	const float h = FontSize + BarH + Pad * 2.0f + 3.0f;
	const float x = std::clamp(m_Width * g_Config.m_NcMapProgressX / 1000.0f - w / 2.0f, 0.0f, m_Width - w);
	const float y = std::clamp(m_Height * g_Config.m_NcMapProgressY / 1000.0f - h / 2.0f, 0.0f, m_Height - h);

	RenderPanel(x, y, w, h);
	StoreRect(WIDGET_PROGRESS, x, y, w, h);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	TextRender()->Text(x + Pad + 2.0f, y + Pad, FontSize, aBuf);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	const float BarX = x + Pad + 2.0f;
	const float BarY = y + Pad + FontSize + 3.0f;
	const float BarW = w - Pad * 2.0f - 2.0f;
	Graphics()->DrawRect(BarX, BarY, BarW, BarH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), IGraphics::CORNER_ALL, 1.5f);
	Graphics()->DrawRect(BarX, BarY, BarW * Progress, BarH, ConfigColor(g_Config.m_NcHudAccentColor).WithAlpha(0.95f), IGraphics::CORNER_ALL, 1.5f);
}

// ---------------------------------------------------------------------------
// CNumbGameBg
// ---------------------------------------------------------------------------

void CNumbGameBg::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(str_comp(m_aLoaded, g_Config.m_NcGameBgImage) != 0)
	{
		str_copy(m_aLoaded, g_Config.m_NcGameBgImage);
		if(m_Texture.IsValid())
			Graphics()->UnloadTexture(&m_Texture);
		m_Width = m_Height = 0;
		if(m_aLoaded[0])
		{
			CImageInfo Image;
			if(Graphics()->LoadPng(Image, m_aLoaded, IStorage::TYPE_ALL_OR_ABSOLUTE))
			{
				m_Width = Image.m_Width;
				m_Height = Image.m_Height;
				m_Texture = Graphics()->LoadTextureRawMove(Image, 0, m_aLoaded);
			}
		}
	}
	if(!m_Texture.IsValid() || m_Width <= 0 || m_Height <= 0 || g_Config.m_NcGameBgImageAlpha <= 0)
		return;

	const float ScreenH = 300.0f;
	const float ScreenW = ScreenH * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(ScreenW, ScreenH);
	const float ImgAspect = m_Width / (float)m_Height;
	float w = ScreenW, h = ScreenW / ImgAspect;
	if(h < ScreenH)
	{
		h = ScreenH;
		w = ScreenH * ImgAspect;
	}
	Graphics()->TextureSet(m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, g_Config.m_NcGameBgImageAlpha / 100.0f);
	const IGraphics::CQuadItem Item((ScreenW - w) / 2.0f, (ScreenH - h) / 2.0f, w, h);
	Graphics()->QuadsDrawTL(&Item, 1);
	Graphics()->QuadsEnd();
}
