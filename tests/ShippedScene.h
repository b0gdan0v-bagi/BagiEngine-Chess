#pragma once

#include <BECore/Config/XmlNode.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <pugixml.hpp>

/// The shipped `config/SceneConfig.xml` of Chess with its node files (`config/nodes/`), found from the source tree: a game's content is not mounted in the test environment, so the
/// folder-based loader is the one to use, not LoadSceneManagerFromConfig. A test reads the scene through these helpers and never with `load_file`, so it sees the same inline tree
/// whichever of the nodes sit in a file of their own
namespace BECore::Chess::Tests {

    inline std::filesystem::path ShippedConfigFolder() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config";
    }

    inline std::filesystem::path ShippedSceneConfig() {
        return ShippedConfigFolder() / "SceneConfig.xml";
    }

    /// Parses the shipped scene and expands its node references into @p document (the plain inline tree, no `sourceRef`); the test fails, and false comes back, when a reference
    /// does not load, an override does not apply or the file is missing
    inline bool LoadShippedSceneDocument(pugi::xml_document& document) {
        const NodeExpansion expansion = LoadSceneDocument(ShippedSceneConfig(), document);
        for (const String& error : expansion.errors) {
            ADD_FAILURE() << "the shipped scene: " << error.c_str();
        }
        return expansion.Ok();
    }

    /// The SceneManager of the shipped scene, built the way the application builds it: LoadSceneManager gets the raw root and expands the references itself (so `sourceRef` is set).
    /// The expansion is checked first, because LoadSceneManager only logs its errors; empty (and a failed test) when the scene does not expand
    inline IntrusivePtrAtomic<SceneManager> LoadShippedSceneManager() {
        pugi::xml_document expanded;
        if (!LoadShippedSceneDocument(expanded)) {
            return {};
        }
        pugi::xml_document raw;
        if (!raw.load_file(ShippedSceneConfig().c_str())) {
            ADD_FAILURE() << "the shipped scene does not parse";
            return {};
        }
        return LoadSceneManager(XmlNode{raw.document_element()}, MakeDirectoryNodeLoader(ShippedConfigFolder()));
    }

}  // namespace BECore::Chess::Tests
