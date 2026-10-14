#ifndef GAME_CLIENT_COMPONENTS_NUMBCLIENT_NUMB_HUD_H
#define GAME_CLIENT_COMPONENTS_NUMBCLIENT_NUMB_HUD_H

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/input.h>

#include <game/client/component.h>

// World-space overlays: hitbox and ninja timer. Rendered after the players.
class CNumbWorld : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

private:
	void RenderHitbox(vec2 Pos);
	void RenderNinjaTimer(int ClientId, vec2 Pos);
	void RenderHammerIndicator();
	void DrawCircleOutline(vec2 Center, float Radius, ColorRGBA Color);
	void DrawArrow(vec2 From, vec2 To, ColorRGBA Color);
};

// Screen-space HUD widgets.
class CNumbHud : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	void ResetSession();
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void OnRelease() override;
	void SetEditorActive(bool Active);
	bool EditorActive() const { return m_EditorActive; }

	int64_t m_SessionStart = 0;
	int m_SessionDeaths = 0;
	int m_SessionFinishes = 0;
	int m_SessionKills = 0;

private:
	float m_Width = 0.0f;
	float m_Height = 300.0f;

	// HUD editor
	enum
	{
		WIDGET_KEYS = 0,
		WIDGET_INPUT,
		WIDGET_SESSION,
		WIDGET_TEAM,
		NUM_WIDGETS
	};
	bool m_EditorActive = false;
	vec2 m_EditorMouse = vec2(0.0f, 0.0f); // in hud coordinates
	int m_DragWidget = -1;
	vec2 m_aDragPos[NUM_WIDGETS];
	float m_aWidgetRect[NUM_WIDGETS][4] = {};
	void StoreRect(int Widget, float x, float y, float w, float h);
	int *WidgetConfigX(int Widget);
	int *WidgetConfigY(int Widget);
	void RenderEditor();
	static void ConHudEditor(IConsole::IResult *pResult, void *pUserData);

	bool ShouldRender() const;
	void RenderPanel(float x, float y, float w, float h);
	void RenderKeyIndicator();
	void RenderInputVisualizer();
	void RenderSessionStats();
	void RenderTeamIndicator();

	static void ConSessionReset(IConsole::IResult *pResult, void *pUserData);
};

#endif
