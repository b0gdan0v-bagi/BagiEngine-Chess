#include "OpponentSettings.h"

#include <BECore/GameManager/CoreManager.h>

namespace BECore::Chess {

    namespace {

        constexpr StringView kWhitespace = " \t\r\n";

        /// The path in the root element @p root without the white space around it; empty when the node, the attribute or the value is missing
        String ReadStockfishPath(const XmlNode& root) {
            if (!root) {
                return {};
            }
            const auto value = root.GetAttribute(OpponentSettings::kStockfishPathAttribute);
            if (!value) {
                return {};
            }
            const size_t first = value->find_first_not_of(kWhitespace);
            if (first == StringView::npos) {
                return {};
            }
            const size_t last = value->find_last_not_of(kWhitespace);
            return String(value->data() + first, last - first + 1);
        }

    }  // namespace

    OpponentSettings OpponentSettings::FromConfig(const XmlNode& shared, const XmlNode& local) {
        OpponentSettings settings;
        settings.stockfishPath = ReadStockfishPath(local);
        if (settings.stockfishPath.empty()) {
            settings.stockfishPath = ReadStockfishPath(shared);
        }
        return settings;
    }

    OpponentSettings OpponentSettings::Load() {
        const ConfigManager& configs = CoreManager::GetConfigManager();
        const auto root = [&configs](StringView name) { return configs.HasConfig(name) ? configs.GetConfig(name) : XmlNode{}; };
        return FromConfig(root(kSharedConfigName), root(kLocalConfigName));
    }

}  // namespace BECore::Chess
