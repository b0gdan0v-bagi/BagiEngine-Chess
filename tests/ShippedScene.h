#pragma once

#include <BECore/Config/XmlNode.h>
#include <BECore/File/Path.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/UI/UIThemes.h>
#include <gtest/gtest.h>
#include <pugixml.hpp>

/// The shipped `config/SceneConfig.xml` of Chess with its node files (`config/nodes/`), found from the source tree: a game's content is not mounted in the test environment, so the
/// folder-based loader is the one to use, not LoadSceneManagerFromConfig. A test reads the scene through these helpers and never with `load_file`, so it sees the same inline tree
/// whichever of the nodes sit in a file of their own
namespace BECore::Chess::Tests {

    inline Path ShippedConfigFolder() {
        return Path(__FILE__).Parent().Parent() / "config";
    }

    inline Path ShippedSceneConfig() {
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
    inline IntrusivePtr<SceneManager> LoadShippedSceneManager() {
        pugi::xml_document expanded;
        if (!LoadShippedSceneDocument(expanded)) {
            return {};
        }
        pugi::xml_document raw;
        if (!raw.load_file(ShippedSceneConfig().CStr())) {
            ADD_FAILURE() << "the shipped scene does not parse";
            return {};
        }
        return LoadSceneManager(XmlNode{raw.document_element()}, MakeDirectoryNodeLoader(ShippedConfigFolder()));
    }

    /// Adds the themes of the shipped `config/UIThemes.xml` to @p themes; the test fails, and false comes back, when the file does not parse or LoadFromXml finds a problem in it
    inline bool LoadShippedThemes(UIThemes& themes) {
        pugi::xml_document document;
        const Path file = ShippedConfigFolder() / "UIThemes.xml";
        if (!document.load_file(file.CStr())) {
            ADD_FAILURE() << "the shipped themes do not parse: " << file.CStr();
            return false;
        }
        const Vector<String> problems = themes.LoadFromXml(XmlNode{document.document_element()});
        for (const String& problem : problems) {
            ADD_FAILURE() << "the shipped themes: " << problem.c_str();
        }
        return problems.empty();
    }

}  // namespace BECore::Chess::Tests
