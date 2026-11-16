#include "ChessMaterialComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Game/BoardOrientation.h>
#include <Chess/Game/CapturedMaterial.h>
#include <Chess/Game/MovePanel.h>
#include <Generated/ChessMaterialComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr size_t kTopGroup = 0;  // the group of the color that is not at the bottom of the board
        constexpr size_t kBottomGroup = 1;

        constexpr size_t kMaterialPanelSet = 1;                   // the set MaterialPanel<Group>...; set 0 is Material<Group>...
        constexpr size_t kRootsPerSet = 2 * kMaterialGroupCount;  // a list and a balance per group

        using RowNames = Array<PoolString, kMaterialGroupCount * kMaterialRowsPerGroup>;

        // Interned on the first use, not per frame; indexed by row like the counts of the component, and by set
        struct MaterialNames {
            RowNames vars;
            Array<RowNames, kMaterialSetCount> rows;
            Array<RowNames, kMaterialSetCount> icons;
            Array<Array<PoolString, kRootsPerSet>, kMaterialSetCount> roots;
            Array<PoolString, kMaterialGroupCount> balanceVars;
        };

        const char* GroupWord(size_t group) {
            return group == kTopGroup ? "Top" : "Bottom";
        }

        const char* SetWord(size_t set) {
            return set == kMaterialPanelSet ? "Panel" : "";
        }

        const MaterialNames& GetNames() {
            static const MaterialNames names = [] {
                MaterialNames made;
                const auto types = CapturableTypes();
                ASSERT(types.size() == kMaterialRowsPerGroup);
                for (size_t group = 0; group < kMaterialGroupCount; ++group) {
                    for (size_t index = 0; index < kMaterialRowsPerGroup; ++index) {
                        const size_t row = group * kMaterialRowsPerGroup + index;
                        const StringView type = EnumUtils<PieceType>::ToString(types[index]);
                        made.vars[row] = PoolString::Intern(Format("material{}{}", GroupWord(group), type).c_str());
                        for (size_t set = 0; set < kMaterialSetCount; ++set) {
                            made.rows[set][row] = PoolString::Intern(Format("Material{}{}{}Row", SetWord(set), GroupWord(group), type).c_str());
                            made.icons[set][row] = PoolString::Intern(Format("Material{}{}{}Icon", SetWord(set), GroupWord(group), type).c_str());
                        }
                    }
                    made.balanceVars[group] = PoolString::Intern(Format("material{}Balance", GroupWord(group)).c_str());
                    for (size_t set = 0; set < kMaterialSetCount; ++set) {
                        made.roots[set][2 * group] = PoolString::Intern(Format("Material{}{}List", SetWord(set), GroupWord(group)).c_str());
                        made.roots[set][2 * group + 1] = PoolString::Intern(Format("Material{}{}Balance", SetWord(set), GroupWord(group)).c_str());
                    }
                }
                return made;
            }();
            return names;
        }

        // The box of the lists is asked per view; the scene's active view is already the one of this draw (it is picked at the start of Draw())
        MaterialView ActiveMaterialView(const Scene& scene) {
            return HasPortraitPlaces(scene.GetActiveView().name) ? MaterialView::Portrait : MaterialView::Default;
        }

        // The color of a group: the lower one is the color at the bottom of the board, the upper one the other
        PieceColor GroupColor(size_t group, PieceColor bottom) {
            return group == kBottomGroup ? bottom : Opposite(bottom);
        }

    }  // namespace

    void ChessMaterialComponent::Publish(const ChessGameModel* model, PieceColor bottom, bool iconsToo) {
        const MaterialNames& names = GetNames();
        const auto types = CapturableTypes();
        // No game: a summary with nothing gone and no points, so one path writes the empty words
        const MaterialSummary summary = model ? SummarizeMaterial(model->GetPosition()) : MaterialSummary{};
        UIVars& vars = CoreManager::GetUIVars();
        UICanvasComponent* canvas = GetNode().GetComponent<UICanvasComponent>().Get();

        for (size_t group = 0; group < kMaterialGroupCount; ++group) {
            // A group shows what its color has taken: the pieces of the other color that are gone
            const PieceColor color = GroupColor(group, bottom);
            const PieceColor taken = Opposite(color);
            for (size_t index = 0; index < kMaterialRowsPerGroup; ++index) {
                const size_t row = group * kMaterialRowsPerGroup + index;
                const int count = summary.GetMissing(taken, types[index]);
                _counts[row] = count;
                const String word = count > 0 ? Format("x{}", count) : String();
                vars.Set(names.vars[row], StringView(word));
                if (iconsToo && canvas) {
                    const PoolString sprite = PoolString::Intern(Format("{}{}", EnumUtils<PieceColor>::ToString(taken), EnumUtils<PieceType>::ToString(types[index])).c_str());
                    for (size_t set = 0; set < kMaterialSetCount; ++set) {
                        if (UIElement* element = canvas->GetRoot().FindByName(names.icons[set][row])) {
                            if (UIImageComponent* icon = element->GetComponent<UIImageComponent>()) {
                                icon->sprite = sprite;
                            }
                        }
                    }
                }
            }
            const String balance = BalanceText(summary.GetPoints(color) - summary.GetPoints(taken));
            vars.Set(names.balanceVars[group], StringView(balance));
        }
    }

    void ChessMaterialComponent::SyncRows() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        // Before the first layout of the canvas the safe area is not known: the margin set stays with no row (its balances as they always were), and the next draw has it
        const UICanvasLayout& layout = canvas->GetLayout();
        bool inPanel = false;
        bool marginFits = false;
        if (layout.HasLayout()) {
            const Rect& safe = layout.GetResolved().safeBounds;
            // The rule of the panel itself, on a canvas of the same space and safe area as the panel's: the panel set is shown exactly while the panel beside the board is, so the material
            // never goes with it. The panel of the Tall view has no bands for the players: the margin set's rows above and below the board have room there
            inPanel = MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, MovePanelView::Wide);
            marginFits = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, ActiveMaterialView(GetScene()));
        }
        const MaterialNames& names = GetNames();
        for (size_t set = 0; set < kMaterialSetCount; ++set) {
            const bool setWanted = (set == kMaterialPanelSet) == inPanel;
            if (setWanted != _setShown[set]) {
                _setShown[set] = setWanted;
                for (const PoolString& root : names.roots[set]) {
                    canvas->SetElementVisible(root, setWanted);
                }
            }
            const bool rowsFit = setWanted && (set == kMaterialPanelSet || marginFits);
            for (size_t row = 0; row < _counts.size(); ++row) {
                const bool wanted = rowsFit && _counts[row] > 0;
                if (wanted != _rowShown[set][row]) {
                    _rowShown[set][row] = wanted;
                    canvas->SetElementVisible(names.rows[set][row], wanted);
                }
            }
        }
    }

    void ChessMaterialComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        // The orientation the board draws with, read on every draw like the board does: it follows the panel, a set_state and a Reset
        const PieceColor bottom = BottomColor(opponent ? opponent->GetPlays() : std::nullopt);
        const uint64_t version = model ? model->GetVersion() : 0;
        const bool bottomChanged = _publishedBottom != bottom;
        if (bottomChanged || model != _publishedModel || version != _publishedVersion) {
            _publishedBottom = bottom;
            _publishedModel = model;
            _publishedVersion = version;
            Publish(model, bottom, bottomChanged);
        }
        SyncRows();
    }

}  // namespace BECore::Chess
