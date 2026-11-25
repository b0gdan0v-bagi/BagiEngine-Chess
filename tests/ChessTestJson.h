#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Common/JsonDoc.h>
#include <Games/IControllableGame.h>
#include <expected>
#include <ostream>

/**
 * A small JSON tree for the tests of the control API. The engine's own code reads through `JsonView` and writes with `JsonWriter` and has no tree any more; a test that builds a request
 * member by member and reads an answer by name is easier with one, so this one lives here, over the engine's tape and writer, and `DomControl` turns it into the texts the adapter takes and gives.
 * Not for the game's code, and not a place for what the engine's reader refuses: a request is written as text and read back by the adapter like a client's.
 */
namespace BECore::Chess::Tests {

    class Json {
    public:
        enum class Kind : uint8_t { Null, Bool, Integer, Number, Text, Array, Object };

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool value) : _kind(Kind::Bool), _integer(value ? 1 : 0) {}
        Json(double value) : _kind(Kind::Number), _number(value) {}
        Json(const char* text) : _kind(Kind::Text), _text(text) {}
        Json(StringView text) : _kind(Kind::Text), _text(text.data(), text.size()) {}
        Json(const String& text) : _kind(Kind::Text), _text(text) {}

        /// Any integer type but bool and the character types
        template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>) Json(T value) : _kind(Kind::Integer), _integer(static_cast<int64_t>(value)) {}

        [[nodiscard]] static Json object() {
            Json json;
            json._kind = Kind::Object;
            return json;
        }

        [[nodiscard]] static Json array() {
            Json json;
            json._kind = Kind::Array;
            return json;
        }

        [[nodiscard]] Kind GetKind() const {
            return _kind;
        }
        [[nodiscard]] bool is_null() const {
            return _kind == Kind::Null;
        }
        [[nodiscard]] bool is_object() const {
            return _kind == Kind::Object;
        }
        [[nodiscard]] bool is_array() const {
            return _kind == Kind::Array;
        }
        [[nodiscard]] bool is_string() const {
            return _kind == Kind::Text;
        }
        [[nodiscard]] bool is_boolean() const {
            return _kind == Kind::Bool;
        }
        [[nodiscard]] bool is_number_integer() const {
            return _kind == Kind::Integer;
        }
        [[nodiscard]] bool is_number() const {
            return _kind == Kind::Integer || _kind == Kind::Number;
        }

        /// Members of an object or items of an array; 0 for a scalar
        [[nodiscard]] size_t size() const {
            return _items.size();
        }
        [[nodiscard]] bool empty() const {
            return _items.empty();
        }

        /// The member @p key; an object that has none gets a null one (a null value becomes an object), like the tree this one stands in for
        Json& operator[](StringView key) {
            if (_kind == Kind::Null) {
                _kind = Kind::Object;
            }
            EXPECT(_kind == Kind::Object, "Json: a member of a value that is not an object");
            const size_t at = LowerBound(key);
            if (at == _keys.size() || StringView(_keys[at]) != key) {
                _keys.insert(_keys.begin() + at, String(key.data(), key.size()));
                _items.insert(_items.begin() + at, Json());
            }
            return _items[at];
        }

        /// A member that is not there reads as null
        const Json& operator[](StringView key) const {
            const Json* found = Find(key);
            return found ? *found : NullValue();
        }

        Json& operator[](size_t index) {
            EXPECT(_kind == Kind::Array && index < _items.size(), "Json: an index of a value that is not an array, or past its end");
            return _items[index];
        }

        const Json& operator[](size_t index) const {
            return _kind == Kind::Array && index < _items.size() ? _items[index] : NullValue();
        }

        void push_back(Json value) {
            if (_kind == Kind::Null) {
                _kind = Kind::Array;
            }
            EXPECT(_kind == Kind::Array, "Json: push_back on a value that is not an array");
            _items.push_back(std::move(value));
        }

        /// The member @p key of an object, nullptr when there is none
        [[nodiscard]] const Json* Find(StringView key) const {
            if (_kind != Kind::Object) {
                return nullptr;
            }
            const size_t at = LowerBound(key);
            return at < _keys.size() && StringView(_keys[at]) == key ? &_items[at] : nullptr;
        }

        [[nodiscard]] Vector<Json>::const_iterator begin() const {
            return _items.begin();
        }
        [[nodiscard]] Vector<Json>::const_iterator end() const {
            return _items.end();
        }

        /// The value as @p T (bool, an integer, double or String); the test fails and a default comes back when the kind is another
        template <typename T>
        [[nodiscard]] T get() const {
            if constexpr (std::is_same_v<T, bool>) {
                EXPECT(_kind == Kind::Bool, "Json: not a boolean");
                return _integer != 0;
            } else if constexpr (std::is_integral_v<T>) {
                EXPECT(_kind == Kind::Integer, "Json: not an integer");
                return static_cast<T>(_integer);
            } else if constexpr (std::is_floating_point_v<T>) {
                EXPECT(is_number(), "Json: not a number");
                return static_cast<T>(_kind == Kind::Integer ? static_cast<double>(_integer) : _number);
            } else {
                static_assert(std::is_same_v<T, String>, "Json::get: bool, an integer, a floating point or String");
                EXPECT(_kind == Kind::Text, "Json: not a string");
                return _text;
            }
        }

        [[nodiscard]] const String& Text() const {
            return _text;
        }
        [[nodiscard]] Optional<int64_t> AsInteger() const {
            return _kind == Kind::Integer ? Optional<int64_t>(_integer) : std::nullopt;
        }
        [[nodiscard]] Optional<double> AsNumber() const {
            return is_number() ? Optional<double>(_kind == Kind::Integer ? static_cast<double>(_integer) : _number) : std::nullopt;
        }
        [[nodiscard]] Optional<bool> AsBool() const {
            return _kind == Kind::Bool ? Optional<bool>(_integer != 0) : std::nullopt;
        }
        [[nodiscard]] const Vector<String>& Keys() const {
            return _keys;
        }

        /// Equal kind, value and members; an integer and a number of the same value are equal
        [[nodiscard]] bool operator==(const Json& other) const {
            if (is_number() && other.is_number()) {
                return AsNumber() == other.AsNumber() && (_kind == other._kind ? true : AsNumber() == other.AsNumber());
            }
            if (_kind != other._kind) {
                return false;
            }
            switch (_kind) {
                case Kind::Null:
                    return true;
                case Kind::Bool:
                case Kind::Integer:
                    return _integer == other._integer;
                case Kind::Number:
                    return _number == other._number;
                case Kind::Text:
                    return _text == other._text;
                case Kind::Array:
                    return _items == other._items;
                case Kind::Object:
                    return _keys == other._keys && _items == other._items;
            }
            return false;
        }

        /// The compact text of the value, the members of an object in byte order of their names
        [[nodiscard]] String dump() const;

    private:
        static const Json& NullValue() {
            static const Json kNull;
            return kNull;
        }

        /// The place of @p key among the sorted member names
        [[nodiscard]] size_t LowerBound(StringView key) const {
            size_t low = 0;
            size_t high = _keys.size();
            while (low < high) {
                const size_t middle = low + (high - low) / 2;
                if (StringView(_keys[middle]) < key) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            return low;
        }

        Kind _kind = Kind::Null;
        int64_t _integer = 0;
        double _number = 0.0;
        String _text;
        Vector<Json> _items;   ///< the items of an array, the values of an object
        Vector<String> _keys;  ///< the names of an object, sorted, one per value
    };

    /// Writes @p value as one JSON value
    inline void WriteJson(JsonWriter& writer, const Json& value) {
        switch (value.GetKind()) {
            case Json::Kind::Null:
                writer.Null();
                return;
            case Json::Kind::Bool:
                writer.Value(value.get<bool>());
                return;
            case Json::Kind::Integer:
                writer.Value(value.get<int64_t>());
                return;
            case Json::Kind::Number:
                writer.Value(value.get<double>());
                return;
            case Json::Kind::Text:
                writer.Value(StringView(value.Text()));
                return;
            case Json::Kind::Array:
                writer.BeginArray();
                for (const Json& item : value) {
                    WriteJson(writer, item);
                }
                writer.EndArray();
                return;
            case Json::Kind::Object: {
                writer.BeginObject();
                size_t at = 0;
                for (const Json& item : value) {
                    writer.Key(StringView(value.Keys()[at++]));
                    WriteJson(writer, item);
                }
                writer.EndObject();
                return;
            }
        }
    }

    inline String Json::dump() const {
        String text;
        JsonWriter writer(text);
        WriteJson(writer, *this);
        return text;
    }

    [[nodiscard]] inline String DumpJson(const Json& value) {
        return value.dump();
    }

    inline std::ostream& operator<<(std::ostream& out, const Json& value) {
        return out << value.dump().c_str();
    }

    /// The tree of what @p view holds; a number that is no integer is a floating point one
    [[nodiscard]] inline Json FromView(JsonView view) {
        switch (view.Kind()) {
            case JsonKind::Null:
                return Json();
            case JsonKind::Bool:
                return Json(view.AsBool().value_or(false));
            case JsonKind::Integer:
                if (const Optional<int64_t> integer = view.AsInt64()) {
                    return Json(*integer);
                }
                return Json(view.AsDouble().value_or(0.0));
            case JsonKind::Number:
                return Json(view.AsDouble().value_or(0.0));
            case JsonKind::String: {
                String text;
                view.ReadText(text);
                return Json(text);
            }
            case JsonKind::Array: {
                Json array = Json::array();
                for (const JsonView item : view.Items()) {
                    array.push_back(FromView(item));
                }
                return array;
            }
            case JsonKind::Object: {
                Json object = Json::object();
                for (const JsonMember member : view.Members()) {
                    String key;
                    member.key.ReadText(key);
                    object[StringView(key)] = FromView(member.value);
                }
                return object;
            }
        }
        return Json();
    }

    [[nodiscard]] inline std::expected<Json, ControlError> ParseJson(StringView text) {
        const ::BECore::Tests::JsonDoc doc(text);
        if (!doc.IsParsed()) {
            return std::unexpected(MakeControlError(ControlErrors::kParseError, "not JSON"));
        }
        return FromView(doc.Root());
    }

    [[nodiscard]] inline String MakeText(const Json& value) {
        return value.dump();
    }

    [[nodiscard]] inline Json MakeJsonString(StringView text) {
        return Json(text);
    }

    [[nodiscard]] inline const Json* FindJsonMember(const Json& object, StringView key) {
        return object.Find(key);
    }

    [[nodiscard]] inline Optional<String> GetJsonString(const Json& object, StringView key) {
        const Json* found = object.Find(key);
        return found && found->is_string() ? Optional<String>(found->Text()) : std::nullopt;
    }

    [[nodiscard]] inline Optional<int64_t> GetJsonInt(const Json& object, StringView key) {
        const Json* found = object.Find(key);
        return found ? found->AsInteger() : std::nullopt;
    }

    [[nodiscard]] inline Optional<double> GetJsonNumber(const Json& object, StringView key) {
        const Json* found = object.Find(key);
        return found ? found->AsNumber() : std::nullopt;
    }

    [[nodiscard]] inline Optional<bool> GetJsonBool(const Json& object, StringView key) {
        const Json* found = object.Find(key);
        return found ? found->AsBool() : std::nullopt;
    }

    /**
     * @brief The calls of an `IControllableGame` with a `Json` tree in and out: each writes the request as text, makes the call and reads the answer back
     *
     * Holds a reference, so a temporary over `*game.AsControllable()` is fine for one statement.
     */
    class DomControl {
    public:
        explicit DomControl(IControllableGame& game) : _game(game) {}

        [[nodiscard]] std::expected<Json, ControlError> GetState() const {
            String text;
            JsonWriter writer(text);
            if (auto written = _game.WriteState(writer); !written) {
                return std::unexpected(std::move(written.error()));
            }
            return Read(text, writer);
        }

        [[nodiscard]] std::expected<Json, ControlError> GetActions() const {
            String text;
            JsonWriter writer(text);
            if (auto written = _game.WriteActions(writer); !written) {
                return std::unexpected(std::move(written.error()));
            }
            return Read(text, writer);
        }

        std::expected<Json, ControlError> PerformAction(const Json& action) {
            const ::BECore::Tests::JsonDoc request(action.dump());
            String text;
            JsonWriter writer(text);
            if (auto done = _game.PerformAction(request.Root(), writer); !done) {
                return std::unexpected(std::move(done.error()));
            }
            return Read(text, writer);
        }

        std::expected<void, ControlError> Reset(const Json& options) {
            const ::BECore::Tests::JsonDoc request(options.dump());
            return _game.Reset(request.Root());
        }

        std::expected<void, ControlError> SetState(const Json& state) {
            const ::BECore::Tests::JsonDoc request(state.dump());
            return _game.SetState(request.Root());
        }

        [[nodiscard]] uint64_t GetStateVersion() const {
            return _game.GetStateVersion();
        }

    private:
        /// The adapter writes exactly one complete value; anything else is a bug of the adapter that the test names
        static std::expected<Json, ControlError> Read(const String& text, const JsonWriter& writer) {
            if (!writer.IsComplete()) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the adapter wrote no complete value"));
            }
            return ParseJson(StringView(text));
        }

        IControllableGame& _game;
    };

}  // namespace BECore::Chess::Tests
