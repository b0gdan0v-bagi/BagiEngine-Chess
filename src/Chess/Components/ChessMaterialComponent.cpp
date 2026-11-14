#include "ChessMaterialComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/BoardOrientation.h>
#include <Chess/Game/CapturedMaterial.h>
#include <Generated/ChessMaterialComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr size_t kTopGroup = 0;  // the group of the color that is not at the bottom of the board
        constexpr size_t kBottomGroup = 1;

        // The interned names of the variables and of the elements of the canvas, built on the first use (an interned name is not made per frame). A row is group * 5 + the index of its piece in
        // CapturableTypes()
        struct MaterialNames {
            Array<PoolString, kMaterialGroupCount * kMaterialRowsPerGroup> vars;   // materialTopQueen ..
            Array<PoolString, kMaterialGroupCount * kMaterialRowsPerGroup> rows;   // MaterialTopQueenRow ..
            Array<PoolString, kMaterialGroupCount * kMaterialRowsPerGroup> icons;  // MaterialTopQueenIcon ..
            Array<PoolString, kMaterialGroupCount> balanceVars;                    // materialTopBalance, materialBottomBalance
        };

        const char* GroupWord(size_t group) {
            return group == kTopGroup ? "Top" : "Bottom";
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
                        made.rows[row] = PoolString::Intern(Format("Material{}{}Row", GroupWord(group), type).c_str());
                        made.icons[row] = PoolString::Intern(Format("Material{}{}Icon", GroupWord(group), type).c_str());
                    }
                    made.balanceVars[group] = PoolString::Intern(Format("material{}Balance", GroupWord(group)).c_str());
                }
                return made;
            }();
            return names;
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
                    if (UIElement* element = canvas->GetRoot().FindByName(names.icons[row])) {
                        if (UIImageComponent* icon = element->GetComponent<UIImageComponent>()) {
                            icon->sprite = PoolString::Intern(Format("{}{}", EnumUtils<PieceColor>::ToString(taken), EnumUtils<PieceType>::ToString(types[index])).c_str());
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
        // Before the first layout of the canvas the safe area is not known, and the rows stay hidden: the next draw has it
        const UICanvasLayout& layout = canvas->GetLayout();
        bool fits = false;
        if (layout.HasLayout()) {
            const Rect& safe = layout.GetResolved().safeBounds;
            fits = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h);
        }
        const MaterialNames& names = GetNames();
        for (size_t row = 0; row < _counts.size(); ++row) {
            const bool wanted = fits && _counts[row] > 0;
            if (wanted != _rowShown[row]) {
                _rowShown[row] = wanted;
                canvas->SetElementVisible(names.rows[row], wanted);
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
