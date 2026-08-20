#pragma once

#include "core/color.h"
#include "light/draw.h"
#include "light/effects/EffectBase.h"
#include "light/layers/Layer.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace mm {
namespace pixelblaze {

constexpr uint8_t kMaxNames = 96;
constexpr uint8_t kNameLen = 28;
constexpr uint16_t kMaxNodes = 520;
constexpr uint16_t kMaxStmts = 260;
constexpr uint8_t kMaxFunctions = 20;
constexpr uint8_t kMaxArgs = 4;
constexpr uint8_t kMaxArrays = 12;
constexpr uint8_t kMaxControls = 10;
constexpr uint16_t kMaxLoopIterations = 4096;
constexpr uint8_t kMaxCallDepth = 8;
constexpr uint32_t kBaseFrameInstructionBudget = 65536;
constexpr uint32_t kInstructionsPerPixel = 96;

enum class Tok : uint8_t {
    End, Ident, Number, LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Plus, Minus, Star, Slash, Percent, Assign, PlusAssign,
    MinusAssign, StarAssign, SlashAssign, PlusPlus, MinusMinus, Less, LessEq,
    Greater, GreaterEq, EqEq, NotEq, AndAnd, OrOr, Bang, Error
};

enum class Op : uint8_t {
    Add, Sub, Mul, Div, Mod, Less, LessEq, Greater, GreaterEq, Eq, NotEq, And, Or
};

enum class AssignOp : uint8_t { Set, Add, Sub, Mul, Div, Inc, Dec };
enum class NodeKind : uint8_t { Number, Var, ArrayGet, UnaryNeg, UnaryNot, Binary, Call };
enum class StmtKind : uint8_t { Empty, Assign, ArrayAssign, Expr, If, For, Return };
enum class ControlKind : uint8_t { SliderFunction, ExportVar };

struct Lexer {
    const char* p = nullptr;
    Tok kind = Tok::End;
    float number = 0.0f;
    char ident[kNameLen] = {};
    const char* err = "";

    explicit Lexer(const char* source) : p(source ? source : "") { advance(); }

    static bool isIdentStart(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }
    static bool isIdent(char c) {
        return isIdentStart(c) || (c >= '0' && c <= '9');
    }
    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    void skipHorizontal() {
        for (;;) {
            while (*p == ' ' || *p == '\t' || *p == '\r') p++;
            if (p[0] == '/' && p[1] == '/') {
                p += 2;
                while (*p && *p != '\n') p++;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) p++;
                if (*p) p += 2;
                continue;
            }
            break;
        }
    }

    void readIdent() {
        uint8_t n = 0;
        while (isIdent(*p)) {
            if (n + 1 < kNameLen) ident[n++] = *p;
            p++;
        }
        ident[n] = '\0';
    }

    void readNumber() {
        char buf[32] = {};
        uint8_t n = 0;
        while (isDigit(*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') {
            if ((*p == '+' || *p == '-') && n > 0 && buf[n - 1] != 'e' && buf[n - 1] != 'E') break;
            if (n + 1 < sizeof(buf)) buf[n++] = *p;
            p++;
        }
        buf[n] = '\0';
        number = std::strtof(buf, nullptr);
    }

    void advance() {
        skipHorizontal();
        if (*p == '\n') { p++; kind = Tok::Semicolon; return; }
        if (*p == '\0') { kind = Tok::End; return; }
        if (isIdentStart(*p)) { readIdent(); kind = Tok::Ident; return; }
        if (isDigit(*p) || (*p == '.' && isDigit(p[1]))) { readNumber(); kind = Tok::Number; return; }

        const char c = *p++;
        switch (c) {
            case '(': kind = Tok::LParen; return;
            case ')': kind = Tok::RParen; return;
            case '{': kind = Tok::LBrace; return;
            case '}': kind = Tok::RBrace; return;
            case '[': kind = Tok::LBracket; return;
            case ']': kind = Tok::RBracket; return;
            case ',': kind = Tok::Comma; return;
            case ';': kind = Tok::Semicolon; return;
            case '+':
                if (*p == '+') { p++; kind = Tok::PlusPlus; return; }
                if (*p == '=') { p++; kind = Tok::PlusAssign; return; }
                kind = Tok::Plus; return;
            case '-':
                if (*p == '-') { p++; kind = Tok::MinusMinus; return; }
                if (*p == '=') { p++; kind = Tok::MinusAssign; return; }
                kind = Tok::Minus; return;
            case '*':
                if (*p == '=') { p++; kind = Tok::StarAssign; return; }
                kind = Tok::Star; return;
            case '/':
                if (*p == '=') { p++; kind = Tok::SlashAssign; return; }
                kind = Tok::Slash; return;
            case '%': kind = Tok::Percent; return;
            case '=':
                if (*p == '=') { p++; kind = Tok::EqEq; return; }
                kind = Tok::Assign; return;
            case '<':
                if (*p == '=') { p++; kind = Tok::LessEq; return; }
                kind = Tok::Less; return;
            case '>':
                if (*p == '=') { p++; kind = Tok::GreaterEq; return; }
                kind = Tok::Greater; return;
            case '!':
                if (*p == '=') { p++; kind = Tok::NotEq; return; }
                kind = Tok::Bang; return;
            case '&':
                if (*p == '&') { p++; kind = Tok::AndAnd; return; }
                break;
            case '|':
                if (*p == '|') { p++; kind = Tok::OrOr; return; }
                break;
            default: break;
        }
        kind = Tok::Error;
        err = "unexpected character";
    }
};

struct Node {
    NodeKind kind = NodeKind::Number;
    float number = 0.0f;
    int16_t name = -1;
    int16_t left = -1;
    int16_t right = -1;
    int16_t args[kMaxArgs] = {-1, -1, -1, -1};
    uint8_t argc = 0;
    Op op = Op::Add;
};

struct Stmt {
    StmtKind kind = StmtKind::Empty;
    int16_t name = -1;
    int16_t index = -1;
    int16_t value = -1;
    int16_t init = -1;
    int16_t post = -1;
    int16_t first = -1;
    int16_t count = 0;
    int16_t elseFirst = -1;
    int16_t elseCount = 0;
    int16_t next = -1;
    AssignOp assign = AssignOp::Set;
};

struct Function {
    int16_t name = -1;
    int16_t params[kMaxArgs] = {-1, -1, -1, -1};
    uint8_t argc = 0;
    int16_t first = -1;
    int16_t count = 0;
    bool exported = false;
};

struct ControlBinding {
    ControlKind kind = ControlKind::SliderFunction;
    int16_t name = -1;
    int16_t function = -1;
    int16_t var = -1;
    uint8_t value = 128;
};

struct ArraySlot {
    uint16_t first = 0;
    uint16_t len = 0;
};

class Program {
public:
    ~Program() { freeArrays(); }

    bool compile(const char* source, uint32_t pixelCount) {
        reset();
        pixelCount_ = pixelCount > 0 ? pixelCount : 1;
        intern("pixelCount");
        intern("PI");
        intern("PI2");
        intern("index");
        intern("x");
        intern("y");
        setVar("pixelCount", static_cast<float>(pixelCount_));
        setVar("PI", kPi);
        setVar("PI2", kPi2);

        Lexer lex(source);
        Parser parser(*this, lex);
        if (!parser.parseProgram()) {
            error_ = parser.error();
            return false;
        }
        beforeRender_ = findFunction("beforeRender");
        render_ = findFunction("render");
        render2D_ = findFunction("render2D");
        if (render_ < 0 && render2D_ < 0) {
            error_ = "no render/render2D function";
            return false;
        }
        discoverControls();
        instructionsRemaining_ = instructionBudget();
        Exec exec(*this, ExecPhase::Initialization);
        if (!exec.range(globalFirst_, globalCount_)) {
            error_ = exec.error();
            return false;
        }
        ok_ = true;
        return true;
    }

    bool ok() const { return ok_; }
    const char* error() const { return error_ ? error_ : "compile failed"; }
    bool hasRender2D() const { return render2D_ >= 0; }
    uint8_t controlCount() const { return controlCount_; }
    const char* controlName(uint8_t i) const {
        return i < controlCount_ && controls_[i].name >= 0 ? names_[controls_[i].name] : "";
    }
    uint8_t& controlValue(uint8_t i) { return controls_[i].value; }
    size_t dynamicBytes() const { return static_cast<size_t>(arrayCapacity_) * sizeof(float); }

    void beginFrame(float deltaMs, uint32_t elapsedMs) {
        if (!ok_) return;
        instructionsRemaining_ = instructionBudget();
        elapsedMs_ = elapsedMs;
        resetOutput();
        for (uint8_t i = 0; i < controlCount_; i++) {
            const float v = static_cast<float>(controls_[i].value) / 255.0f;
            if (controls_[i].kind == ControlKind::ExportVar && controls_[i].var >= 0) {
                values_[controls_[i].var] = v;
            } else if (controls_[i].function >= 0) {
                callFunction(controls_[i].function, &v, 1);
            }
        }
        resetTransform();
        if (beforeRender_ >= 0) {
            float args[1] = {deltaMs};
            callFunction(beforeRender_, args, 1);
        }
    }

    RGB renderPixel(uint32_t index, float x, float y) {
        resetOutput();
        if (!ok_) return {0, 0, 0};
        setVar("index", static_cast<float>(index));
        if (render2D_ >= 0) {
            float tx = x;
            float ty = y;
            applyTransform(tx, ty);
            setVar("x", tx);
            setVar("y", ty);
            float args[3] = {static_cast<float>(index), tx, ty};
            callFunction(render2D_, args, 3);
        } else if (render_ >= 0) {
            float args[1] = {static_cast<float>(index)};
            callFunction(render_, args, 1);
        }
        return outputSet_ ? output_ : RGB{0, 0, 0};
    }

private:
    friend struct Parser;
    friend struct Exec;

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kPi2 = 6.28318530717958647692f;

    struct Parser {
        Program& p;
        Lexer& lex;
        const char* err = nullptr;

        Parser(Program& program, Lexer& lexer) : p(program), lex(lexer) {}
        const char* error() const { return err ? err : (lex.err[0] ? lex.err : "parse error"); }
        void fail(const char* msg) { if (!err) err = msg; }

        bool accept(Tok t) {
            if (lex.kind != t) return false;
            lex.advance();
            return true;
        }
        bool expect(Tok t, const char* msg) {
            if (lex.kind != t) { fail(msg); return false; }
            lex.advance();
            return true;
        }
        bool atIdent(const char* s) const {
            return lex.kind == Tok::Ident && std::strcmp(lex.ident, s) == 0;
        }
        void skipTerminators() {
            while (lex.kind == Tok::Semicolon) lex.advance();
        }
        void endStatement() {
            while (lex.kind == Tok::Semicolon) lex.advance();
        }

        bool parseProgram() {
            skipTerminators();
            int16_t last = -1;
            while (!err && lex.kind != Tok::End && lex.kind != Tok::Error) {
                bool exported = false;
                if (atIdent("export")) { exported = true; lex.advance(); }
                if (atIdent("function")) {
                    if (!parseFunction(exported)) return false;
                } else {
                    int16_t stmt = parseStatement(exported, true);
                    if (stmt < 0) return false;
                    link(p.globalFirst_, p.globalCount_, last, stmt);
                }
                skipTerminators();
            }
            if (lex.kind == Tok::Error) fail(lex.err);
            return !err;
        }

        bool parseFunction(bool exported) {
            lex.advance();
            if (lex.kind != Tok::Ident) { fail("expected function name"); return false; }
            const int16_t name = p.intern(lex.ident);
            lex.advance();
            if (!expect(Tok::LParen, "expected '('")) return false;
            Function fn;
            fn.name = name;
            fn.exported = exported;
            if (lex.kind != Tok::RParen) {
                while (!err) {
                    if (fn.argc >= kMaxArgs) { fail("too many parameters"); return false; }
                    if (lex.kind != Tok::Ident) { fail("expected parameter name"); return false; }
                    fn.params[fn.argc++] = p.intern(lex.ident);
                    lex.advance();
                    if (!accept(Tok::Comma)) break;
                }
            }
            if (!expect(Tok::RParen, "expected ')'")) return false;
            if (!expect(Tok::LBrace, "expected function body")) return false;
            if (!parseBlock(fn.first, fn.count)) return false;
            if (p.functionCount_ >= kMaxFunctions) { fail("too many functions"); return false; }
            p.functions_[p.functionCount_++] = fn;
            return true;
        }

        bool parseBlock(int16_t& first, int16_t& count) {
            first = -1;
            count = 0;
            int16_t last = -1;
            skipTerminators();
            while (!err && lex.kind != Tok::RBrace && lex.kind != Tok::End) {
                bool exported = false;
                if (atIdent("export")) { exported = true; lex.advance(); }
                int16_t stmt = parseStatement(exported, true);
                if (stmt < 0) return false;
                link(first, count, last, stmt);
                skipTerminators();
            }
            if (!expect(Tok::RBrace, "expected '}'")) return false;
            return true;
        }

        bool parseBody(int16_t& first, int16_t& count) {
            if (accept(Tok::LBrace)) return parseBlock(first, count);
            first = -1;
            count = 0;
            int16_t last = -1;
            const int16_t stmt = parseStatement(false, true);
            if (stmt < 0) return false;
            link(first, count, last, stmt);
            return true;
        }

        void link(int16_t& first, int16_t& count, int16_t& last, int16_t stmt) {
            if (stmt < 0) return;
            p.stmts_[stmt].next = -1;
            if (first < 0) first = stmt;
            if (last >= 0) p.stmts_[last].next = stmt;
            last = stmt;
            count++;
        }

        int16_t append(const Stmt& s) {
            if (p.stmtCount_ >= kMaxStmts) { fail("script too large"); return -1; }
            const int16_t idx = p.stmtCount_;
            p.stmts_[p.stmtCount_++] = s;
            return idx;
        }

        int16_t parseStatement(bool exported, bool consumeEnd) {
            skipTerminators();
            if (lex.kind == Tok::LBrace) {
                lex.advance();
                Stmt s;
                s.kind = StmtKind::Expr;
                if (!parseBlock(s.first, s.count)) return -1;
                return append(s);
            }
            if (atIdent("if")) return parseIf();
            if (atIdent("for")) return parseFor();
            if (atIdent("return")) {
                lex.advance();
                Stmt s;
                s.kind = StmtKind::Return;
                s.value = parseExpr();
                if (s.value < 0) return -1;
                if (consumeEnd) endStatement();
                return append(s);
            }

            bool varDecl = false;
            if (atIdent("var")) { varDecl = true; lex.advance(); }
            if (atIdent("uint8_t")) { varDecl = true; lex.advance(); }

            if (lex.kind == Tok::Ident) {
                char name[kNameLen] = {};
                std::snprintf(name, sizeof(name), "%.*s",
                              static_cast<int>(sizeof(name) - 1), lex.ident);
                lex.advance();
                if (lex.kind == Tok::LBracket) {
                    lex.advance();
                    const int16_t indexExpr = parseExpr();
                    if (!expect(Tok::RBracket, "expected ']'")) return -1;
                    AssignOp op = AssignOp::Set;
                    if (!parseAssignOp(op)) return -1;
                    Stmt s;
                    s.kind = StmtKind::ArrayAssign;
                    s.name = p.intern(name);
                    s.index = indexExpr;
                    s.value = (op == AssignOp::Inc || op == AssignOp::Dec) ? -1 : parseExpr();
                    s.assign = op;
                    if (s.value < 0 && op != AssignOp::Inc && op != AssignOp::Dec) return -1;
                    if (consumeEnd) endStatement();
                    return append(s);
                }
                if (isAssignLike(lex.kind)) {
                    AssignOp op = AssignOp::Set;
                    if (!parseAssignOp(op)) return -1;
                    Stmt s;
                    s.kind = StmtKind::Assign;
                    s.name = p.intern(name);
                    s.value = (op == AssignOp::Inc || op == AssignOp::Dec) ? -1 : parseExpr();
                    s.assign = op;
                    if (s.value < 0 && op != AssignOp::Inc && op != AssignOp::Dec) return -1;
                    if (exported && varDecl && op == AssignOp::Set) p.addExportVarControl(s.name);
                    if (consumeEnd) endStatement();
                    return append(s);
                }
                const int16_t head = parseIdentContinuation(name);
                if (head < 0) return -1;
                Stmt s;
                s.kind = StmtKind::Expr;
                s.value = parseBinaryRhs(0, head);
                if (s.value < 0) return -1;
                if (consumeEnd) endStatement();
                return append(s);
            }

            Stmt s;
            s.kind = StmtKind::Expr;
            s.value = parseExpr();
            if (s.value < 0) return -1;
            if (consumeEnd) endStatement();
            return append(s);
        }

        bool parseAssignOp(AssignOp& op) {
            switch (lex.kind) {
                case Tok::Assign: op = AssignOp::Set; lex.advance(); return true;
                case Tok::PlusAssign: op = AssignOp::Add; lex.advance(); return true;
                case Tok::MinusAssign: op = AssignOp::Sub; lex.advance(); return true;
                case Tok::StarAssign: op = AssignOp::Mul; lex.advance(); return true;
                case Tok::SlashAssign: op = AssignOp::Div; lex.advance(); return true;
                case Tok::PlusPlus: op = AssignOp::Inc; lex.advance(); return true;
                case Tok::MinusMinus: op = AssignOp::Dec; lex.advance(); return true;
                default: fail("expected assignment"); return false;
            }
        }

        static bool isAssignLike(Tok t) {
            return t == Tok::Assign || t == Tok::PlusAssign || t == Tok::MinusAssign ||
                   t == Tok::StarAssign || t == Tok::SlashAssign ||
                   t == Tok::PlusPlus || t == Tok::MinusMinus;
        }

        int16_t parseIf() {
            lex.advance();
            if (!expect(Tok::LParen, "expected '(' after if")) return -1;
            Stmt s;
            s.kind = StmtKind::If;
            s.value = parseExpr();
            if (!expect(Tok::RParen, "expected ')'")) return -1;
            if (!parseBody(s.first, s.count)) return -1;
            skipTerminators();
            if (atIdent("else")) {
                lex.advance();
                if (atIdent("if")) {
                    s.elseFirst = parseIf();
                    if (s.elseFirst < 0) return -1;
                    s.elseCount = 1;
                } else {
                    if (!parseBody(s.elseFirst, s.elseCount)) return -1;
                }
            }
            return append(s);
        }

        int16_t parseFor() {
            lex.advance();
            if (!expect(Tok::LParen, "expected '(' after for")) return -1;
            Stmt s;
            s.kind = StmtKind::For;
            s.init = parseStatement(false, false);
            if (!expect(Tok::Semicolon, "expected ';' in for")) return -1;
            s.value = parseExpr();
            if (!expect(Tok::Semicolon, "expected ';' in for")) return -1;
            s.post = parseStatement(false, false);
            if (!expect(Tok::RParen, "expected ')' after for")) return -1;
            if (!parseBody(s.first, s.count)) return -1;
            return append(s);
        }

        int precedence(Tok t) const {
            switch (t) {
                case Tok::OrOr: return 1;
                case Tok::AndAnd: return 2;
                case Tok::EqEq:
                case Tok::NotEq: return 3;
                case Tok::Less:
                case Tok::LessEq:
                case Tok::Greater:
                case Tok::GreaterEq: return 4;
                case Tok::Plus:
                case Tok::Minus: return 5;
                case Tok::Star:
                case Tok::Slash:
                case Tok::Percent: return 6;
                default: return 0;
            }
        }

        Op opFor(Tok t) const {
            switch (t) {
                case Tok::Minus: return Op::Sub;
                case Tok::Star: return Op::Mul;
                case Tok::Slash: return Op::Div;
                case Tok::Percent: return Op::Mod;
                case Tok::Less: return Op::Less;
                case Tok::LessEq: return Op::LessEq;
                case Tok::Greater: return Op::Greater;
                case Tok::GreaterEq: return Op::GreaterEq;
                case Tok::EqEq: return Op::Eq;
                case Tok::NotEq: return Op::NotEq;
                case Tok::AndAnd: return Op::And;
                case Tok::OrOr: return Op::Or;
                default: return Op::Add;
            }
        }

        int16_t node(const Node& n) {
            if (p.nodeCount_ >= kMaxNodes) { fail("script too complex"); return -1; }
            const int16_t idx = p.nodeCount_;
            p.nodes_[p.nodeCount_++] = n;
            return idx;
        }

        int16_t parseExpr() {
            const int16_t lhs = parseUnary();
            if (lhs < 0) return -1;
            return parseBinaryRhs(0, lhs);
        }

        int16_t parseBinaryRhs(int minPrec, int16_t lhs) {
            while (!err) {
                const int prec = precedence(lex.kind);
                if (prec < minPrec || prec == 0) return lhs;
                const Tok opTok = lex.kind;
                lex.advance();
                int16_t rhs = parseUnary();
                if (rhs < 0) return -1;
                const int nextPrec = precedence(lex.kind);
                if (prec < nextPrec) rhs = parseBinaryRhs(prec + 1, rhs);
                Node n;
                n.kind = NodeKind::Binary;
                n.left = lhs;
                n.right = rhs;
                n.op = opFor(opTok);
                lhs = node(n);
            }
            return -1;
        }

        int16_t parseUnary() {
            if (accept(Tok::Minus)) {
                Node n;
                n.kind = NodeKind::UnaryNeg;
                n.left = parseUnary();
                return n.left >= 0 ? node(n) : -1;
            }
            if (accept(Tok::Bang)) {
                Node n;
                n.kind = NodeKind::UnaryNot;
                n.left = parseUnary();
                return n.left >= 0 ? node(n) : -1;
            }
            return parsePrimary();
        }

        int16_t parsePrimary() {
            if (lex.kind == Tok::Number) {
                Node n;
                n.kind = NodeKind::Number;
                n.number = lex.number;
                lex.advance();
                return node(n);
            }
            if (lex.kind == Tok::Ident) {
                char name[kNameLen] = {};
                std::snprintf(name, sizeof(name), "%.*s",
                              static_cast<int>(sizeof(name) - 1), lex.ident);
                lex.advance();
                return parseIdentContinuation(name);
            }
            if (accept(Tok::LParen)) {
                int16_t e = parseExpr();
                if (!expect(Tok::RParen, "expected ')'")) return -1;
                return e;
            }
            fail("expected expression");
            return -1;
        }

        int16_t parseIdentContinuation(const char* name) {
            if (accept(Tok::LParen)) {
                Node n;
                n.kind = NodeKind::Call;
                n.name = p.intern(name);
                if (lex.kind != Tok::RParen) {
                    while (!err) {
                        if (n.argc >= kMaxArgs) { fail("too many call arguments"); return -1; }
                        n.args[n.argc++] = parseExpr();
                        if (!accept(Tok::Comma)) break;
                    }
                }
                if (!expect(Tok::RParen, "expected ')'")) return -1;
                return node(n);
            }
            if (accept(Tok::LBracket)) {
                Node n;
                n.kind = NodeKind::ArrayGet;
                n.name = p.intern(name);
                n.left = parseExpr();
                if (!expect(Tok::RBracket, "expected ']'")) return -1;
                return node(n);
            }
            Node n;
            n.kind = NodeKind::Var;
            n.name = p.intern(name);
            return node(n);
        }
    };

    enum class ExecPhase : uint8_t { Initialization, Frame };

    struct Exec {
        Program& p;
        ExecPhase phase;
        const char* err = nullptr;
        bool returned = false;
        float returnValue = 0.0f;
        uint8_t depth = 0;

        explicit Exec(Program& program, ExecPhase executionPhase = ExecPhase::Frame)
            : p(program), phase(executionPhase) {}
        const char* error() const { return err ? err : "runtime error"; }
        void fail(const char* msg) {
            if (err) return;
            err = msg;
            p.error_ = msg;
            p.ok_ = false;
        }

        bool consumeInstruction() {
            if (p.instructionsRemaining_ == 0) {
                fail("instruction budget hit");
                return false;
            }
            p.instructionsRemaining_--;
            return true;
        }

        bool range(int16_t first, int16_t count) {
            if (first < 0 || count < 0) return true;
            int16_t current = first;
            for (int16_t i = 0; !err && i < count && current >= 0; i++) {
                const int16_t next = p.stmts_[current].next;
                stmt(p.stmts_[current]);
                if (returned) break;
                current = next;
            }
            return !err;
        }

        void stmt(const Stmt& s) {
            if (!consumeInstruction()) return;
            switch (s.kind) {
                case StmtKind::Empty: break;
                case StmtKind::Expr:
                    if (s.first >= 0 && s.count > 0) { range(s.first, s.count); break; }
                    (void)eval(s.value);
                    break;
                case StmtKind::Assign: assignVar(s); break;
                case StmtKind::ArrayAssign: assignArray(s); break;
                case StmtKind::If:
                    if (truth(eval(s.value))) range(s.first, s.count);
                    else range(s.elseFirst, s.elseCount);
                    break;
                case StmtKind::For: {
                    stmt(p.stmts_[s.init]);
                    uint16_t guard = 0;
                    while (!err && truth(eval(s.value))) {
                        range(s.first, s.count);
                        if (returned) break;
                        stmt(p.stmts_[s.post]);
                        if (++guard > kMaxLoopIterations) { fail("loop limit hit"); break; }
                    }
                    break;
                }
                case StmtKind::Return:
                    returnValue = eval(s.value);
                    returned = true;
                    break;
            }
        }

        void assignVar(const Stmt& s) {
            if (p.assignArrayCall(s.name, s.value, *this)) return;
            const float cur = p.values_[s.name];
            p.setValue(s.name, applyAssign(cur, s.assign, s.value));
        }

        void assignArray(const Stmt& s) {
            const int16_t array = p.arrayFor(s.name);
            if (array < 0) return;
            const int idx = static_cast<int>(eval(s.index));
            if (idx < 0 || idx >= p.arrays_[array].len) return;
            float* slot = &p.arrayData_[p.arrays_[array].first + static_cast<uint16_t>(idx)];
            *slot = applyAssign(*slot, s.assign, s.value);
        }

        float applyAssign(float cur, AssignOp op, int16_t valueNode) {
            const float v = valueNode >= 0 ? eval(valueNode) : 0.0f;
            switch (op) {
                case AssignOp::Set: return v;
                case AssignOp::Add: return cur + v;
                case AssignOp::Sub: return cur - v;
                case AssignOp::Mul: return cur * v;
                case AssignOp::Div: return v != 0.0f ? cur / v : 0.0f;
                case AssignOp::Inc: return cur + 1.0f;
                case AssignOp::Dec: return cur - 1.0f;
            }
            return cur;
        }

        bool truth(float v) const { return v > 0.00001f || v < -0.00001f; }

        float eval(int16_t idx) {
            if (!consumeInstruction()) return 0.0f;
            if (idx < 0 || idx >= p.nodeCount_) return 0.0f;
            const Node& n = p.nodes_[idx];
            switch (n.kind) {
                case NodeKind::Number: return n.number;
                case NodeKind::Var: return p.values_[n.name];
                case NodeKind::UnaryNeg: return -eval(n.left);
                case NodeKind::UnaryNot: return truth(eval(n.left)) ? 0.0f : 1.0f;
                case NodeKind::ArrayGet: {
                    const int16_t array = p.arrayFor(n.name);
                    if (array < 0) return 0.0f;
                    const int pos = static_cast<int>(eval(n.left));
                    if (pos < 0 || pos >= p.arrays_[array].len) return 0.0f;
                    return p.arrayData_[p.arrays_[array].first + static_cast<uint16_t>(pos)];
                }
                case NodeKind::Binary: return evalBinary(n);
                case NodeKind::Call: return call(n);
            }
            return 0.0f;
        }

        float evalBinary(const Node& n) {
            const float a = eval(n.left);
            if (n.op == Op::And && !truth(a)) return 0.0f;
            if (n.op == Op::Or && truth(a)) return 1.0f;
            const float b = eval(n.right);
            switch (n.op) {
                case Op::Add: return a + b;
                case Op::Sub: return a - b;
                case Op::Mul: return a * b;
                case Op::Div: return b != 0.0f ? a / b : 0.0f;
                case Op::Mod: return b != 0.0f ? std::fmod(a, b) : 0.0f;
                case Op::Less: return a < b ? 1.0f : 0.0f;
                case Op::LessEq: return a <= b ? 1.0f : 0.0f;
                case Op::Greater: return a > b ? 1.0f : 0.0f;
                case Op::GreaterEq: return a >= b ? 1.0f : 0.0f;
                case Op::Eq: return std::fabs(a - b) < 0.0001f ? 1.0f : 0.0f;
                case Op::NotEq: return std::fabs(a - b) >= 0.0001f ? 1.0f : 0.0f;
                case Op::And: return truth(b) ? 1.0f : 0.0f;
                case Op::Or: return truth(b) ? 1.0f : 0.0f;
            }
            return 0.0f;
        }

        float call(const Node& n) {
            float args[kMaxArgs] = {};
            for (uint8_t i = 0; i < n.argc; i++) args[i] = eval(n.args[i]);
            return p.callByName(n.name, args, n.argc, this);
        }
    };

    void reset() {
        freeArrays();
        nameCount_ = 0;
        nodeCount_ = 0;
        stmtCount_ = 0;
        globalFirst_ = -1;
        globalCount_ = 0;
        functionCount_ = 0;
        controlCount_ = 0;
        beforeRender_ = -1;
        render_ = -1;
        render2D_ = -1;
        ok_ = false;
        error_ = nullptr;
        rng_ = 0x1234abcdU;
        instructionsRemaining_ = 0;
        for (uint8_t i = 0; i < kMaxNames; i++) {
            values_[i] = 0.0f;
            varArrays_[i] = -1;
        }
        resetTransform();
    }

    void freeArrays() {
        platform::free(arrayData_);
        arrayData_ = nullptr;
        arrayCapacity_ = 0;
        arrayUsed_ = 0;
        arrayCount_ = 0;
        for (uint8_t i = 0; i < kMaxArrays; i++) arrays_[i] = {};
    }

    int16_t intern(const char* name) {
        if (!name || !name[0]) return -1;
        for (uint8_t i = 0; i < nameCount_; i++)
            if (std::strcmp(names_[i], name) == 0) return i;
        if (nameCount_ >= kMaxNames) return 0;
        std::snprintf(names_[nameCount_], kNameLen, "%.*s",
                      static_cast<int>(kNameLen - 1), name);
        return nameCount_++;
    }

    void setVar(const char* name, float value) {
        const int16_t id = intern(name);
        if (id >= 0) values_[id] = value;
    }

    void setValue(int16_t id, float value) {
        if (id < 0 || id >= nameCount_) return;
        values_[id] = value;
    }

    bool isCall(int16_t node, const char* name) const {
        if (node < 0 || node >= nodeCount_) return false;
        const Node& n = nodes_[node];
        return n.kind == NodeKind::Call && n.name >= 0 && std::strcmp(names_[n.name], name) == 0;
    }

    bool assignArrayCall(int16_t var, int16_t valueNode, Exec& exec) {
        if (var < 0 || valueNode < 0 || valueNode >= nodeCount_) return false;
        const Node& n = nodes_[valueNode];
        if (isCall(valueNode, "array")) {
            if (exec.phase != ExecPhase::Initialization) {
                exec.fail("arrays must be initialized at top level");
                return true;
            }
            const float requested = n.argc ? exec.eval(n.args[0]) : static_cast<float>(pixelCount_);
            if (!std::isfinite(requested) || requested > 32768.0f) {
                exec.fail("array size out of range");
                return true;
            }
            const uint16_t len = requested > 0.0f ? static_cast<uint16_t>(requested) : 1;
            if (allocateArray(var, len) < 0) exec.fail("array allocation failed");
            return true;
        }
        if (isCall(valueNode, "makePIController")) {
            if (exec.phase != ExecPhase::Initialization) {
                exec.fail("arrays must be initialized at top level");
                return true;
            }
            const int16_t array = allocateArray(var, 5);
            if (array >= 0) {
                for (uint8_t i = 0; i < n.argc && i < 5; i++) {
                    arrayData_[arrays_[array].first + i] = exec.eval(n.args[i]);
                }
            } else exec.fail("array allocation failed");
            return true;
        }
        return false;
    }

    int16_t findFunction(const char* name) const {
        for (uint8_t i = 0; i < functionCount_; i++) {
            if (functions_[i].name >= 0 && std::strcmp(names_[functions_[i].name], name) == 0)
                return i;
        }
        return -1;
    }

    int16_t findFunctionByNameId(int16_t name) const {
        for (uint8_t i = 0; i < functionCount_; i++)
            if (functions_[i].name == name) return i;
        return -1;
    }

    int16_t arrayFor(int16_t var) const {
        if (var < 0 || var >= nameCount_) return -1;
        return varArrays_[var];
    }

    bool ensureArrayCapacity(uint16_t wanted) {
        if (wanted <= arrayCapacity_) return true;
        uint16_t next = arrayCapacity_ == 0 ? 512 : arrayCapacity_;
        while (next < wanted && next < 32768) next = static_cast<uint16_t>(next * 2);
        const size_t bytes = static_cast<size_t>(next) * sizeof(float);
        const size_t maxBlock = platform::maxAllocBlock();
        if (maxBlock != 0 && bytes > maxBlock) return false;
        float* fresh = static_cast<float*>(
            platform::alloc(bytes));
        if (!fresh) return false;
        for (uint16_t i = 0; i < next; i++) fresh[i] = i < arrayUsed_ ? arrayData_[i] : 0.0f;
        platform::free(arrayData_);
        arrayData_ = fresh;
        arrayCapacity_ = next;
        return true;
    }

    int16_t allocateArray(int16_t var, uint16_t len) {
        if (var < 0) return -1;
        if (arrayCount_ >= kMaxArrays) return -1;
        if (len == 0) len = 1;
        const uint32_t wanted = static_cast<uint32_t>(arrayUsed_) + len;
        if (wanted > 32768u || !ensureArrayCapacity(static_cast<uint16_t>(wanted))) return -1;
        const int16_t array = arrayCount_++;
        arrays_[array] = {arrayUsed_, len};
        for (uint16_t i = 0; i < len; i++) arrayData_[arrayUsed_ + i] = 0.0f;
        arrayUsed_ = static_cast<uint16_t>(arrayUsed_ + len);
        varArrays_[var] = array;
        values_[var] = 0.0f;
        return array;
    }

    void addExportVarControl(int16_t var) {
        if (var < 0 || controlCount_ >= kMaxControls) return;
        for (uint8_t i = 0; i < controlCount_; i++)
            if (controls_[i].kind == ControlKind::ExportVar && controls_[i].var == var) return;
        ControlBinding& c = controls_[controlCount_++];
        c.kind = ControlKind::ExportVar;
        c.name = var;
        c.var = var;
        c.value = 128;
    }

    void discoverControls() {
        for (uint8_t i = 0; i < functionCount_ && controlCount_ < kMaxControls; i++) {
            const Function& fn = functions_[i];
            if (!fn.exported || fn.argc != 1 || fn.name < 0) continue;
            const char* name = names_[fn.name];
            if (std::strncmp(name, "slider", 6) != 0 || name[6] == '\0') continue;
            ControlBinding& c = controls_[controlCount_++];
            c.kind = ControlKind::SliderFunction;
            c.function = i;
            c.name = fn.name;
            c.value = 128;
        }
    }

    float callFunction(int16_t fnIndex, const float* args, uint8_t argc) {
        if (fnIndex < 0 || fnIndex >= functionCount_) return 0.0f;
        Exec exec(*this);
        exec.depth = callDepth_;
        return callFunction(fnIndex, args, argc, &exec);
    }

    float callFunction(int16_t fnIndex, const float* args, uint8_t argc, Exec* parent) {
        if (fnIndex < 0 || fnIndex >= functionCount_) return 0.0f;
        if (callDepth_ >= kMaxCallDepth) return 0.0f;
        const Function& fn = functions_[fnIndex];
        float saved[kMaxArgs] = {};
        for (uint8_t i = 0; i < fn.argc; i++) {
            saved[i] = values_[fn.params[i]];
            values_[fn.params[i]] = i < argc ? args[i] : 0.0f;
        }
        callDepth_++;
        Exec local(*this);
        Exec& exec = parent ? *parent : local;
        exec.returned = false;
        exec.returnValue = 0.0f;
        exec.range(fn.first, fn.count);
        callDepth_--;
        for (uint8_t i = 0; i < fn.argc; i++) values_[fn.params[i]] = saved[i];
        const float ret = exec.returnValue;
        exec.returned = false;
        return ret;
    }

    float callByName(int16_t name, const float* args, uint8_t argc, Exec* exec) {
        if (name < 0) return 0.0f;
        const char* s = names_[name];
        if (std::strcmp(s, "array") == 0) return 0.0f;
        if (std::strcmp(s, "floor") == 0) return argc ? std::floor(args[0]) : 0.0f;
        if (std::strcmp(s, "abs") == 0) return argc ? std::fabs(args[0]) : 0.0f;
        if (std::strcmp(s, "sin") == 0) return argc ? std::sin(args[0]) : 0.0f;
        if (std::strcmp(s, "cos") == 0) return argc ? std::cos(args[0]) : 0.0f;
        if (std::strcmp(s, "sqrt") == 0) return argc && args[0] > 0.0f ? std::sqrt(args[0]) : 0.0f;
        if (std::strcmp(s, "log") == 0) return argc && args[0] > 0.0f ? std::log(args[0]) : 0.0f;
        if (std::strcmp(s, "pow") == 0) return argc >= 2 ? std::pow(args[0], args[1]) : 0.0f;
        if (std::strcmp(s, "min") == 0) return argc >= 2 && args[0] < args[1] ? args[0] : (argc >= 2 ? args[1] : 0.0f);
        if (std::strcmp(s, "max") == 0) return argc >= 2 && args[0] > args[1] ? args[0] : (argc >= 2 ? args[1] : 0.0f);
        if (std::strcmp(s, "clamp") == 0) return argc >= 3 ? clamp(args[0], args[1], args[2]) : 0.0f;
        if (std::strcmp(s, "random") == 0) return random01() * (argc ? args[0] : 1.0f);
        if (std::strcmp(s, "time") == 0) return timeFn(argc ? args[0] : 1.0f);
        if (std::strcmp(s, "wave") == 0) return argc ? 0.5f + 0.5f * std::sin(args[0] * kPi2) : 0.0f;
        if (std::strcmp(s, "triangle") == 0) return argc ? triangle(args[0]) : 0.0f;
        if (std::strcmp(s, "perlin") == 0) return perlin(args, argc);
        if (std::strcmp(s, "hsv") == 0) { if (argc >= 3) hsv(args[0], args[1], args[2]); return 0.0f; }
        if (std::strcmp(s, "rgb") == 0) { if (argc >= 3) rgb(args[0], args[1], args[2]); return 0.0f; }
        if (std::strcmp(s, "resetTransform") == 0) { resetTransform(); return 0.0f; }
        if (std::strcmp(s, "translate") == 0) { if (argc >= 2) translate(args[0], args[1]); return 0.0f; }
        if (std::strcmp(s, "rotate") == 0) { if (argc >= 1) rotate(args[0]); return 0.0f; }
        if (std::strcmp(s, "makePIController") == 0) return 0.0f;

        const int16_t fn = findFunctionByNameId(name);
        if (fn >= 0) return callFunction(fn, args, argc, exec);
        return 0.0f;
    }

    static float clamp(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static float fract(float v) {
        v = v - std::floor(v);
        return v < 0.0f ? v + 1.0f : v;
    }

    static float triangle(float v) {
        const float f = fract(v);
        return f < 0.5f ? f * 2.0f : (1.0f - f) * 2.0f;
    }

    float random01() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>((rng_ >> 8) & 0x00ffffffU) / 16777216.0f;
    }

    float timeFn(float speed) const {
        return fract(static_cast<float>(elapsedMs_) * speed / 1000.0f);
    }

    float perlin(const float* args, uint8_t argc) const {
        float n = 0.0f;
        for (uint8_t i = 0; i < argc; i++) n += args[i] * (12.9898f + 31.17f * static_cast<float>(i));
        return 0.5f + 0.5f * std::sin(n);
    }

    void hsv(float h, float s, float v) {
        const uint8_t hue = static_cast<uint8_t>(fract(h) * 255.0f);
        const uint8_t sat = static_cast<uint8_t>(clamp(s, 0.0f, 1.0f) * 255.0f);
        const uint8_t val = static_cast<uint8_t>(clamp(v, 0.0f, 1.0f) * 255.0f);
        output_ = hsvToRgb(hue, sat, val);
        outputSet_ = true;
    }

    void rgb(float r, float g, float b) {
        output_ = {
            static_cast<uint8_t>(clamp(r, 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(clamp(g, 0.0f, 1.0f) * 255.0f),
            static_cast<uint8_t>(clamp(b, 0.0f, 1.0f) * 255.0f),
        };
        outputSet_ = true;
    }

    void resetOutput() {
        output_ = {0, 0, 0};
        outputSet_ = false;
    }

    void resetTransform() {
        ta_ = 1.0f; tb_ = 0.0f; tc_ = 0.0f; td_ = 1.0f; tx_ = 0.0f; ty_ = 0.0f;
    }

    void translate(float x, float y) {
        tx_ += x;
        ty_ += y;
    }

    void rotate(float angle) {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float na = ta_ * c + tb_ * s;
        const float nb = -ta_ * s + tb_ * c;
        const float nc = tc_ * c + td_ * s;
        const float nd = -tc_ * s + td_ * c;
        ta_ = na; tb_ = nb; tc_ = nc; td_ = nd;
    }

    void applyTransform(float& x, float& y) const {
        const float px = x + tx_;
        const float py = y + ty_;
        x = ta_ * px + tb_ * py;
        y = tc_ * px + td_ * py;
    }

    uint32_t instructionBudget() const {
        const uint64_t scaled = static_cast<uint64_t>(pixelCount_) * kInstructionsPerPixel;
        if (scaled <= kBaseFrameInstructionBudget) return kBaseFrameInstructionBudget;
        return scaled > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(scaled);
    }

    char names_[kMaxNames][kNameLen] = {};
    float values_[kMaxNames] = {};
    int16_t varArrays_[kMaxNames] = {};
    Node nodes_[kMaxNodes] = {};
    Stmt stmts_[kMaxStmts] = {};
    Function functions_[kMaxFunctions] = {};
    ControlBinding controls_[kMaxControls] = {};
    ArraySlot arrays_[kMaxArrays] = {};
    float* arrayData_ = nullptr;
    uint16_t arrayCapacity_ = 0;
    uint16_t arrayUsed_ = 0;
    uint32_t pixelCount_ = 1;
    uint32_t elapsedMs_ = 0;
    uint32_t rng_ = 0x1234abcdU;
    uint32_t instructionsRemaining_ = 0;
    uint8_t nameCount_ = 0;
    int16_t nodeCount_ = 0;
    int16_t stmtCount_ = 0;
    int16_t globalFirst_ = -1;
    int16_t globalCount_ = 0;
    uint8_t functionCount_ = 0;
    uint8_t arrayCount_ = 0;
    uint8_t controlCount_ = 0;
    uint8_t callDepth_ = 0;
    int16_t beforeRender_ = -1;
    int16_t render_ = -1;
    int16_t render2D_ = -1;
    bool ok_ = false;
    const char* error_ = nullptr;
    RGB output_{0, 0, 0};
    bool outputSet_ = false;
    float ta_ = 1.0f, tb_ = 0.0f, tc_ = 0.0f, td_ = 1.0f, tx_ = 0.0f, ty_ = 0.0f;
};

}  // namespace pixelblaze

/// Pixelblaze-compatible scripted effect for sources from Electromage PatternDB.
///
/// The effect accepts the common Pixelblaze source shape: top-level globals, `beforeRender`,
/// `render(index)` and `render2D(index,x,y)`, numeric arrays, loops, conditionals, math helpers,
/// transform helpers, and `hsv`/`rgb` output. It is intentionally bounded: scripts that exceed the
/// fixed parser budgets or use unsupported JavaScript features report a module status and render
/// dark instead of crashing the controller. Slider-style exported functions become regular
/// projectMM controls.
class PixelblazeCompatEffect : public EffectBase {
public:
    const char* tags() const override { return "PB"; }
    Dim dimensions() const override { return Dim::D2; }

    void defineControls() override {
        controls_.addTextArea("source", source_, sizeof(source_));
        for (uint8_t i = 0; i < program_.controlCount(); i++) {
            controlNames_[i][0] = '\0';
            const char* raw = program_.controlName(i);
            const char* label = raw;
            if (std::strncmp(raw, "slider", 6) == 0 && raw[6]) label = raw + 6;
            std::snprintf(controlNames_[i], sizeof(controlNames_[i]), "%.*s",
                          static_cast<int>(sizeof(controlNames_[i]) - 1), label);
            controls_.addUint8(controlNames_[i], program_.controlValue(i), 0, 255);
        }
    }

    bool affectsPrepare(const char* controlName) const override {
        return std::strcmp(controlName, "source") == 0;
    }

    void prepare() override {
        const uint32_t pixels = static_cast<uint32_t>(width()) * static_cast<uint32_t>(height());
        if (program_.compile(source_, pixels > 0 ? pixels : static_cast<uint32_t>(nrOfLights()))) {
            clearStatus();
        } else {
            setStatus(program_.error(), Severity::Error);
        }
        rebuildControls();
        setDynamicBytes(program_.dynamicBytes());
        lastElapsed_ = elapsed();
    }

    void tick() MM_NONBLOCKING override {
        uint8_t* raw = buffer();
        const lengthType w = width();
        const lengthType h = height();
        const uint8_t cpl = channelsPerLight();
        if (!raw || w <= 0 || h <= 0 || cpl == 0) return;
        const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * cpl;
        if (!program_.ok()) {
            std::memset(raw, 0, frameBytes);
            return;
        }

        const uint32_t now = elapsed();
        const float delta = lastElapsed_ == 0 ? 16.0f : static_cast<float>(now - lastElapsed_);
        lastElapsed_ = now;
        program_.beginFrame(delta, now);
        if (!program_.ok()) {
            std::memset(raw, 0, frameBytes);
            setStatus(program_.error(), Severity::Error);
            return;
        }

        for (lengthType y = 0; y < h; y++) {
            for (lengthType x = 0; x < w; x++) {
                const uint32_t idx = static_cast<uint32_t>(y) * static_cast<uint32_t>(w) + static_cast<uint32_t>(x);
                const float nx = w > 1 ? (static_cast<float>(x) + 0.5f) / static_cast<float>(w) : 0.0f;
                const float ny = h > 1 ? (static_cast<float>(y) + 0.5f) / static_cast<float>(h) : 0.0f;
                const RGB c = program_.renderPixel(idx, nx, ny);
                const size_t off = static_cast<size_t>(idx) * cpl;
                raw[off + 0] = c.r;
                if (cpl >= 2) raw[off + 1] = c.g;
                if (cpl >= 3) raw[off + 2] = c.b;
            }
        }
        setDynamicBytes(program_.dynamicBytes());
        if (!program_.ok()) {
            std::memset(raw, 0, frameBytes);
            setStatus(program_.error(), Severity::Error);
        }
    }

    void release() override {
        program_.compile("", 1);
        MoonModule::release();
    }

private:
    pixelblaze::Program program_;
    uint32_t lastElapsed_ = 0;
    char controlNames_[pixelblaze::kMaxControls][pixelblaze::kNameLen] = {};
    char source_[8192] =
        "export function render2D(index, x, y) {\n"
        "  cx = x - 0.5\n"
        "  cy = y - 0.5\n"
        "  r = sqrt(cx * cx + cy * cy)\n"
        "  h = r * 2 + time(.08)\n"
        "  v = wave(r * 7 - time(.35))\n"
        "  hsv(h, 1, v * v)\n"
        "}\n";
};

}  // namespace mm
