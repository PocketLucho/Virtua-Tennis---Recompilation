// "Virtua Tennis Recompiled" product layer for the standalone (SDL) build.
// - one executable for two recompiled NAOMI cartridges, Virtua Tennis (vtennis) and
//   Virtua Tennis 2 (vtenis2c); a launcher screen picks one before booting
// - explains clearly which user files are missing
// - window title / branding
#include "vnba_product.h"
#include "types.h"
#include "stdclass.h"
#include "emulator.h"
#include "cfg/option.h"
#include "oslib/oslib.h"
#include "hw/sh4/aot/aot_boot.h"
#include "hw/sh4/aot/aot_dll_api.h"
#include "ui/gui.h"
#include "ui/gui_util.h"
#include "ui/IconsFontAwesome6.h"
#include "imgui.h"
#include "oslib/i18n.h"
#include <nowide/cstdio.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace i18n;

namespace {

struct ProductGame
{
	const char *id;			// romset name (MAME)
	const char *title;
	const char *subtitle;
	std::string path;		// romset found on disk ("" if missing)
	bool built = false;		// its code is in the game module
};

ProductGame games[] = {
	{ "vtennis", "VIRTUA TENNIS", "Sega / Hitmaker, 1999" },
	{ "vtenis2c", "VIRTUA TENNIS 2", "Sega / Hitmaker, 2001" },
};

bool fileExists(const std::string& path)
{
	FILE *f = nowide::fopen(path.c_str(), "rb");
	if (f == nullptr)
		return false;
	fclose(f);
	return true;
}

void showError(const std::string& msg)
{
	ERROR_LOG(BOOT, "%s", msg.c_str());
#ifdef USE_SDL
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, VNBA_PRODUCT_NAME, msg.c_str(), nullptr);
#else
	fprintf(stderr, "%s\n", msg.c_str());
#endif
}

std::string romsDir()
{
	return get_writable_config_path("roms/");
}

std::string findRomset(const char *id)
{
	const std::vector<std::string> candidates = {
		get_writable_config_path(std::string("roms/") + id + ".zip"),
		std::string("roms/") + id + ".zip",
		get_writable_config_path(std::string(id) + ".zip"),
		get_writable_data_path(std::string(id) + ".zip"),
	};
	for (const std::string& c : candidates)
		if (fileExists(c))
			return c;
	return {};
}

// The builder writes the romsets it generated code for into this file
std::vector<std::string> builtGames()
{
	std::vector<std::string> list;
	std::string file = get_writable_data_path(VNBA_GAME_LIST);
	FILE *f = nowide::fopen(file.c_str(), "r");
	if (f == nullptr)
	{
		return list;
	}
	char line[128];
	while (fgets(line, sizeof(line), f) != nullptr)
	{
		std::string s = line;
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
			s.pop_back();
		if (!s.empty())
			list.push_back(s);
	}
	fclose(f);
	return list;
}

void scanGames()
{
	std::vector<std::string> built = builtGames();
	for (ProductGame& g : games)
	{
		g.path = findRomset(g.id);
		// no list (developer build with the blocks linked in): assume everything is there
		g.built = aot_table_count() != 0 && (built.empty() || std::find(built.begin(), built.end(), g.id) != built.end());
	}
}

bool biosAvailable()
{
	return fileExists(get_writable_data_path("naomi.zip")) || fileExists(get_writable_config_path("roms/naomi.zip"))
			|| fileExists(get_writable_config_path("naomi.zip"));
}

bool snapshotAvailable(const ProductGame& g)
{
	if (const char *p = getenv("VNBA_BOOT_SNAPSHOT"))
		return fileExists(p);
	for (int r : { (int)config::Region, 1, 2, 0, 3 })	// any region: the engine falls back to it
		if (fileExists(aot_boot_snapshot_path(g.id, r)))
			return true;
	return false;
}

std::string launchMessage;

const char *buildScript()
{
#ifdef _WIN32
	return "BUILD_GAME.bat";
#else
	return "./build_game.sh";
#endif
}

void launch(ProductGame& g)
{
	if (g.path.empty())
	{
		launchMessage = strprintf(T("Game not found: copy %s.zip to\n%s"), g.id, romsDir().c_str());
		return;
	}
	if (!g.built)
	{
		launchMessage = strprintf(T("The recompiled code of this game has not been built yet.\nRun %s (once) to build it from your ROM."), buildScript());
		return;
	}
	if (!snapshotAvailable(g) && !biosAvailable())
	{
		launchMessage = strprintf(T("First start of this game: the NAOMI BIOS (naomi.zip) is missing.\n"
				"It is only needed once. Copy it to:\n%s"), get_writable_data_path("naomi.zip").c_str());
		return;
	}
	launchMessage.clear();
	NOTICE_LOG(BOOT, "%s: starting %s", VNBA_PRODUCT_NAME, g.path.c_str());
	gui_start_game(g.path);
}

} // namespace

// Returns false if nothing can be started (the caller should exit).
bool vnba_product_init()
{
	// Builder, last step: merge the code dumped for each game into the game module sources
	if (const char *outDir = getenv("VNBA_SPLIT"))
	{
		bool aot_split_dump(const char *dumpPaths, const char *outDir, int units);
		const char *inputs = getenv("VNBA_SPLIT_INPUTS");
		bool ok = inputs != nullptr && aot_split_dump(inputs, outDir, 48);
		fflush(stderr);
		std::_Exit(ok ? 0 : 2);
	}
	scanGames();
	// Builder: boot one game directly to generate its code (VNBA_START_GAME=<romset>)
	if (const char *id = getenv("VNBA_START_GAME"))
	{
		std::string path = findRomset(id);
		if (path.empty())
		{
			showError(std::string("Romset not found: ") + id + ".zip");
			return false;
		}
		settings.content.path = path;
		return true;
	}
	bool any = false;
	for (const ProductGame& g : games)
		any |= !g.path.empty();
	if (!any && settings.content.path.empty())
	{
		showError("No se encuentra ningún juego / No game found.\n\n"
			"Copia tus romsets en la carpeta \"roms\" junto a este ejecutable.\n"
			"Copy your romsets to the \"roms\" folder next to this executable:\n\n"
			"  vtennis.zip   (Virtua Tennis)\n"
			"  vtenis2c.zip  (Virtua Tennis 2)\n\n" + romsDir());
		return false;
	}
	// The recompiled game code is not distributed: each user builds it from their own ROMs
	if (aot_table_count() == 0 && getenv("VNBA_GENERATE") == nullptr && getenv("VNBA_AOT") == nullptr)
	{
		showError(std::string("Falta el código del juego (" VNBA_GAME_MODULE ").\n"
			"Game code module (" VNBA_GAME_MODULE ") not found.\n\n")
#ifdef _WIN32
			+ "Ejecuta CONSTRUIR_JUEGO.bat (una sola vez) para generarlo a partir de tus ROMs.\n"
			"Run BUILD_GAME.bat once to build it from your own ROMs.");
#else
			+ "Ejecuta ./build_game.sh (una sola vez) para generarlo a partir de tus ROMs.\n"
			"Run ./build_game.sh once to build it from your own ROMs.");
#endif
		return false;
	}
	return true;
}

// Launcher screen (GuiState::Main): pick a game
void vnba_product_launcher()
{
	static bool scanned;
	if (!scanned)
	{
		scanned = true;
		scanGames();
	}
	fullScreenWindow(false);
	ImguiStyleVar _(ImGuiStyleVar_WindowRounding, 0);
	ImguiStyleVar _1(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(10, 40, 24, 255));
	ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	// court lines as a backdrop
	{
		const ImU32 line = IM_COL32(255, 255, 255, 28);
		const float th = uiScaled(3);
		float x0 = origin.x + avail.x * 0.08f, x1 = origin.x + avail.x * 0.92f;
		float y0 = origin.y + avail.y * 0.12f, y1 = origin.y + avail.y * 0.95f;
		dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), line, 0, 0, th);
		dl->AddLine(ImVec2((x0 + x1) / 2, y0), ImVec2((x0 + x1) / 2, y1), line, th);
		dl->AddLine(ImVec2(x0 + (x1 - x0) * 0.2f, (y0 + y1) / 2), ImVec2(x1 - (x1 - x0) * 0.2f, (y0 + y1) / 2), line, th);
		dl->AddLine(ImVec2(x0 + (x1 - x0) * 0.2f, y0), ImVec2(x0 + (x1 - x0) * 0.2f, y1), line, th);
		dl->AddLine(ImVec2(x1 - (x1 - x0) * 0.2f, y0), ImVec2(x1 - (x1 - x0) * 0.2f, y1), line, th);
	}

	// title
	{
		extern ImFont *boldFont;
		ImGui::PushFont(boldFont, uiScaled(52));
		const char *title = "VIRTUA TENNIS - RECOMP";
		ImVec2 ts = ImGui::CalcTextSize(title);
		ImGui::SetCursorPos(ImVec2((avail.x - ts.x) / 2, avail.y * 0.06f));
		ImGui::TextColored(ImVec4(1.f, 0.85f, 0.1f, 1.f), "%s", title);
		ImGui::PopFont();
		const char *sub = T("Choose a game");
		ImVec2 ss = ImGui::CalcTextSize(sub);
		ImGui::SetCursorPosX((avail.x - ss.x) / 2);
		ImGui::TextColored(ImVec4(0.85f, 0.95f, 0.85f, 1.f), "%s", sub);
	}

	// game cards
	const float cardW = std::min(uiScaled(380), avail.x * 0.42f);
	const float cardH = std::min(uiScaled(230), avail.y * 0.45f);
	const float gap = uiScaled(40);
	const float startX = (avail.x - (cardW * 2 + gap)) / 2;
	const float cardY = avail.y * 0.30f;
	for (int i = 0; i < 2; i++)
	{
		ProductGame& g = games[i];
		const bool ok = !g.path.empty() && g.built;
		ImGui::SetCursorPos(ImVec2(startX + i * (cardW + gap), cardY));
		ImGui::PushID(i);
		ImGui::PushStyleColor(ImGuiCol_Button, ok ? ImVec4(0.05f, 0.30f, 0.62f, 0.95f) : ImVec4(0.25f, 0.25f, 0.25f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ok ? ImVec4(0.10f, 0.45f, 0.85f, 1.f) : ImVec4(0.32f, 0.32f, 0.32f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.75f, 0.05f, 1.f));
		ImguiStyleVar r(ImGuiStyleVar_FrameRounding, uiScaled(14));
		if (i == 0 && ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		if (ImGui::Button("##card", ImVec2(cardW, cardH)))
			launch(g);
		ImGui::PopStyleColor(3);
		ImVec2 p0 = ImGui::GetItemRectMin();
		{
			extern ImFont *boldFont;
			ImGui::PushFont(boldFont, uiScaled(34));
			ImVec2 ts = ImGui::CalcTextSize(g.title);
			dl->AddText(ImVec2(p0.x + (cardW - ts.x) / 2, p0.y + cardH * 0.22f), IM_COL32(255, 255, 255, 255), g.title);
			ImGui::PopFont();
		}
		{
			ImVec2 ts = ImGui::CalcTextSize(g.subtitle);
			dl->AddText(ImVec2(p0.x + (cardW - ts.x) / 2, p0.y + cardH * 0.52f), IM_COL32(200, 225, 255, 255), g.subtitle);
			std::string status = ok ? T("PLAY") : g.path.empty() ? strprintf(T("Missing roms/%s.zip"), g.id) : std::string(T("Run the builder"));
			ts = ImGui::CalcTextSize(status.c_str());
			dl->AddText(ImVec2(p0.x + (cardW - ts.x) / 2, p0.y + cardH * 0.75f),
					ok ? IM_COL32(255, 220, 30, 255) : IM_COL32(255, 120, 100, 255), status.c_str());
		}
		ImGui::PopID();
	}

	// message (missing files)
	if (!launchMessage.empty())
	{
		ImVec2 ts = ImGui::CalcTextSize(launchMessage.c_str());
		ImGui::SetCursorPos(ImVec2((avail.x - ts.x) / 2, cardY + cardH + uiScaled(20)));
		ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "%s", launchMessage.c_str());
	}

	// bottom buttons
	{
		ImguiStyleVar pad(ImGuiStyleVar_FramePadding, ScaledVec2(20, 10));
		IconButton settingsBtn(ICON_FA_GEAR, T("Settings"));
		IconButton exitBtn(ICON_FA_POWER_OFF, T("Exit"));
		float w = settingsBtn.width() + exitBtn.width() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetCursorPos(ImVec2((avail.x - w) / 2, avail.y - uiScaled(70)));
		if (exitBtn.realize())
			dc_exit();
		ImGui::SameLine();
		if (settingsBtn.realize())
			gui_setState(GuiState::Settings);
	}
	ImGui::End();
}
