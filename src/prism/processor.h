#pragma once

#include <utility>
#include <vector>
#include <string>
#include <variant>
#include <sstream>
#include <optional>
#include <unordered_map>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include "lexer.h"
#include "ast.h"
#include "utils/exceptions.h"

#define is_type(var, type) std::holds_alternative<type>((var))
#define M_ARRAY(arr, type, ...)                 \
    prism::MTDArray<type> {                     \
        (uintptr_t) &arr, std::vector<size_t> { \
            __VA_ARGS__                         \
        }                                       \
    }
#define VAR(name, type)             \
    {                               \
        name, prism::ContextTypes { \
            type                    \
        }                           \
    }

#define CONTAINS(_map, value) ((_map).find(value) != (_map).end())

namespace prism {
// todo: handle out of bounds access
template <typename T> struct MTDArray {
    uintptr_t ptr = 0;
    std::vector<size_t> dimensions;

    T& at(int x) {
        if (x >= dimensions[0]) {
            throw RuntimeError("Index out of bounds");
        }
        return ((T*) ptr)[x];
    }

    T& at(int x, int y) {
        if (x >= dimensions[0] || y >= dimensions[1]) {
            throw RuntimeError("Index out of bounds");
        }
        return get(x).at(y);
    }

    T& at(int x, int y, int z) {
        if (x >= dimensions[0] || y >= dimensions[1] || z >= dimensions[2]) {
            throw RuntimeError("Index out of bounds");
        }
        return get(x, y).at(z);
    }

    T& at(int x, int y, int z, int w) {
        if (x >= dimensions[0] || y >= dimensions[1] || z >= dimensions[2] || w >= dimensions[3]) {
            throw RuntimeError("Index out of bounds");
        }
        return get(x, y, z).at(w);
    }

    MTDArray<T> get(int x) {
        if (x >= dimensions[0]) {
            throw RuntimeError("Index out of bounds");
        }
        auto ptr = this->ptr;
        auto offset = x * sizeof(T);
        for (size_t i = 0; i < dimensions.size()-1; i++) {
            offset *= dimensions[i+1];
        }
        ptr += offset;
        return MTDArray<T>{ ptr,
                            std::vector<size_t>{ dimensions.begin() + 1, dimensions.end() } };
    }

    MTDArray<T> get(int x, int y) {
        if (x >= dimensions[0] || y >= dimensions[1]) {
            throw RuntimeError("Index out of bounds");
        }
        return get(x).get(y);
    }

    MTDArray<T> get(int x, int y, int z) {
        if (x >= dimensions[0] || y >= dimensions[1] || z >= dimensions[2]) {
            throw RuntimeError("Index out of bounds");
        }
        return get(x, y).get(z);
    }
};

struct GeneratedRange {
    size_t start;
    size_t end;
};

struct SettingDecl {
    std::string var;
    std::string name;
    std::string type; // "float" (default), "int", "toggle", "enum", "color"
    float def = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    // type == "enum": parallel label/value lists parsed from
    // options='Label A:0|Label B:1'; def selects by value.
    std::vector<std::string> optionLabels;
    std::vector<float> optionValues;
    // type == "color": RGB default parsed from default='r, g, b'
    float defColor[3] = { 0.0f, 0.0f, 0.0f };
};

std::string format_float_literal(float v);

struct ForContext {
    std::string name;
    std::variant<GeneratedRange, MTDArray<bool>, MTDArray<int>, MTDArray<float>> iterator;
};

struct Void {};
struct Opaque {
    uintptr_t ptr;
};

// Holds the std::function a native callback was adapted to.
struct InvokeCallable;

// Handle for a native function callable from a template.
class InvokeFunc {
  public:
    InvokeFunc() = default;

    // Accepts either a callable with the erased signature
    //   ContextTypes(ContextItems&, const std::vector<ContextTypes>&),
    // or a plain function pointer
    //   ContextTypes(*)(ContextItems&, const ContextTypes&, ...)
    // whose arguments are unpacked for it and checked for arity.
    // Explicit so std::variant does not consider it when converting other types.
    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, InvokeFunc>>>
    explicit InvokeFunc(F func);

    explicit operator bool() const {
        return m_impl != nullptr;
    }

    const std::shared_ptr<InvokeCallable>& target() const {
        return m_impl;
    }

  private:
    std::shared_ptr<InvokeCallable> m_impl;
};

typedef std::variant<Void, int, float, MTDArray<bool>, MTDArray<int>, MTDArray<float>, GeneratedRange,
                     std::string, ForContext, InvokeFunc, Opaque>
    ContextTypes;
typedef std::unordered_map<std::string, ContextTypes> ContextItems;

struct InvokeCallable {
    std::function<ContextTypes(ContextItems&, const std::vector<ContextTypes>&)> func;
};

namespace detail {
template <typename F> struct MakeCallable {
    static std::shared_ptr<InvokeCallable> make(F func) {
        return std::make_shared<InvokeCallable>(InvokeCallable{ std::move(func) });
    }
};

template <typename... A> struct MakeCallable<ContextTypes (*)(ContextItems&, A...)> {
    typedef ContextTypes (*Func)(ContextItems&, A...);

    static std::shared_ptr<InvokeCallable> make(Func func) {
        return std::make_shared<InvokeCallable>(
            InvokeCallable{ [func](ContextItems& items, const std::vector<ContextTypes>& args) -> ContextTypes {
                if (args.size() != sizeof...(A)) {
                    throw RuntimeError("Native function expects " + std::to_string(sizeof...(A)) +
                                       " argument(s), got " + std::to_string(args.size()));
                }
                return apply(func, items, args, std::index_sequence_for<A...>{});
            } });
    }

  private:
    template <size_t... I>
    static ContextTypes apply(Func func, ContextItems& items, const std::vector<ContextTypes>& args,
                              std::index_sequence<I...>) {
        return func(items, args[I]...);
    }
};
} // namespace detail

template <typename F, typename>
InvokeFunc::InvokeFunc(F func) : m_impl(detail::MakeCallable<std::decay_t<F>>::make(std::move(func))) {
}

inline ContextTypes call_native(const InvokeFunc& func, ContextItems& items, const std::vector<ContextTypes>& args) {
    if (!func) {
        throw RuntimeError("Call to an unbound native function");
    }
    return func.target()->func(items, args);
}

enum class ScopeType { None, If, Else, ElseIf, For };

enum class ExpressionType { None, Variable, If, Else, ElseIf, For, End };

struct Node;
struct RootNode {
    std::shared_ptr<std::vector<std::shared_ptr<Node>>> children;
};
struct TextNode {
    std::string text;
};
struct VariableNode {
    std::shared_ptr<ast::ASTNode> name;
};
struct ElseNode {
    std::shared_ptr<std::vector<std::shared_ptr<Node>>> children;
};
struct ElseIfNode {
    std::shared_ptr<ast::ASTNode> condition;
    std::shared_ptr<std::vector<std::shared_ptr<Node>>> children;
    std::shared_ptr<Node> parentIf;
};
struct IfNode {
    std::shared_ptr<ast::ASTNode> condition;
    std::shared_ptr<std::vector<std::shared_ptr<Node>>> children;
    std::shared_ptr<Node> elseBody;
    std::vector<std::shared_ptr<Node>> elseIfs;
};
struct ForNode {
    std::shared_ptr<ast::ASTNode> condition;
    std::shared_ptr<std::vector<std::shared_ptr<Node>>> children;
};
struct EndNode {};

typedef std::variant<RootNode, TextNode, VariableNode, IfNode, ElseIfNode, ElseNode, ForNode, EndNode> NodeType;

void delete_node(std::shared_ptr<prism::Node>& node);

class Node {
  public:
    Node(NodeType node, std::shared_ptr<Node> parent) : node(std::move(node)), parent(std::move(parent)) {
    }
    NodeType node;
    std::shared_ptr<Node> parent;
    int depth = 0;
};

struct RuntimeContext {
    ScopeType scope = ScopeType::None;
    bool skipUntilEnd = false;
};

typedef std::function<std::optional<std::string>(const std::string&)> IncludeFunc;

class Processor {
  public:
    void populate(const ContextItems& items);
    void load(const std::string& input);
    std::string parse_header(const std::string& data);
    prism::Node parse(std::string input);
    ContextTypes evaluate(const std::shared_ptr<prism::ast::ASTNode>& node);
    void evaluate_node(std::shared_ptr<std::vector<std::shared_ptr<prism::Node>>>& children);
    std::string process();
    ContextItems getTypes() {
        return this->m_items;
    }

    const std::vector<SettingDecl>& settings() const {
        return m_settings;
    }
    void bind_include_loader(IncludeFunc func){
        m_include_loader = std::move(func);
    }

    template <typename T> void array_iterate(prism::ForNode& node, prism::ForContext& context) {
        auto var = context.name;
        auto array = std::get<prism::MTDArray<T>>(context.iterator);
        for (size_t i = 0; i < array.dimensions[0]; i++) {
            m_items[var] = prism::ContextTypes{ array.at(i) };
            evaluate_node(node.children);
        }
        m_items.erase(var);
    }

  private:
    ContextItems m_items;
    std::vector<SettingDecl> m_settings;
    RuntimeContext m_context;
    std::stringstream m_output;
    std::string m_input;
    std::shared_ptr<prism::Node> m_root;
    IncludeFunc m_include_loader;
};
} // namespace prism