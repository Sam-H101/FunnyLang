"""Recursive-descent statement parser + precedence-climbing expression parser
(PLAN.md §M2), covering the full grammar of §3 except `squad` (arrives in M9)."""
from __future__ import annotations

from .ast_nodes import (
    Assign, Binary, Block, Break, Call, Chuck, Coalesce, Continue, Export,
    ExprStmt, ForEach, ForRange, FuncDecl, Get, GroupChatLit, Identifier, If,
    Import, Index, Lambda, Literal, Logical, Me, Og, Param, Pipe, Program,
    Return, SafeGet, Set, SetIndex, Slice, SquadDecl, StashLit, TemplateString,
    Ternary, Try, Unary, VarDecl, ConstDecl, VibeStmt, While, Yap,
)
from .errors import ParseErrorBundle, ParserHadAStroke
from .lexer import Lexer
from .source import SourceFile, Span
from .tokens import KEYWORDS, RESERVED_FUTURE, Token, TokenKind as TK

_KEYWORD_KINDS: frozenset = frozenset(KEYWORDS.values())

# Precedence levels (higher binds tighter). `**` and unary are handled outside
# this table by parse_power()/parse_unary() since unary binds *tighter* than
# `**` per PLAN.md §3.4 (`-2 ** 2` parses as `(-2) ** 2`, not Python's usual
# convention) and `**` needs its own right-associative wiring.
_PIPE, _COALESCE, _OR, _AND = 3, 4, 5, 6
_BITOR, _BITXOR, _BITAND = 7, 8, 9
_EQUALITY, _COMPARISON, _SHIFT = 10, 11, 12
_ADDITIVE, _MULTIPLICATIVE = 13, 14


def _bin(op: str):
    return lambda left, right, span: Binary(op, left, right, span)


def _logical(op: str):
    return lambda left, right, span: Logical(op, left, right, span)


def _pipe(left, right, span):
    return Pipe(left, right, span)


def _coalesce(left, right, span):
    return Coalesce(left, right, span)


# TokenKind -> (left_binding_power, next_min_bp_for_rhs, node_constructor)
BINOP_TABLE: dict[TK, tuple[int, int, object]] = {
    TK.PIPE_GT: (_PIPE, _PIPE + 1, _pipe),
    TK.QUESTION_QUESTION: (_COALESCE, _COALESCE + 1, _coalesce),
    TK.PIPE_PIPE: (_OR, _OR + 1, _logical("||")),
    TK.ORR: (_OR, _OR + 1, _logical("||")),
    TK.AMP_AMP: (_AND, _AND + 1, _logical("&&")),
    TK.FR: (_AND, _AND + 1, _logical("&&")),
    TK.PIPE: (_BITOR, _BITOR + 1, _bin("|")),
    TK.CARET: (_BITXOR, _BITXOR + 1, _bin("^")),
    TK.AMP: (_BITAND, _BITAND + 1, _bin("&")),
    TK.EQ_EQ: (_EQUALITY, _EQUALITY + 1, _bin("==")),
    TK.BANG_EQ: (_EQUALITY, _EQUALITY + 1, _bin("!=")),
    TK.SAME_ENERGY: (_EQUALITY, _EQUALITY + 1, _bin("==")),
    TK.DIFF_ENERGY: (_EQUALITY, _EQUALITY + 1, _bin("!=")),
    TK.LT: (_COMPARISON, _COMPARISON + 1, _bin("<")),
    TK.LE: (_COMPARISON, _COMPARISON + 1, _bin("<=")),
    TK.GT: (_COMPARISON, _COMPARISON + 1, _bin(">")),
    TK.GE: (_COMPARISON, _COMPARISON + 1, _bin(">=")),
    TK.IN: (_COMPARISON, _COMPARISON + 1, _bin("in")),
    TK.LT_LT: (_SHIFT, _SHIFT + 1, _bin("<<")),
    TK.GT_GT: (_SHIFT, _SHIFT + 1, _bin(">>")),
    TK.PLUS: (_ADDITIVE, _ADDITIVE + 1, _bin("+")),
    TK.MINUS: (_ADDITIVE, _ADDITIVE + 1, _bin("-")),
    TK.STAR: (_MULTIPLICATIVE, _MULTIPLICATIVE + 1, _bin("*")),
    TK.SLASH: (_MULTIPLICATIVE, _MULTIPLICATIVE + 1, _bin("/")),
    TK.BACKSLASH: (_MULTIPLICATIVE, _MULTIPLICATIVE + 1, _bin("\\")),
    TK.PERCENT: (_MULTIPLICATIVE, _MULTIPLICATIVE + 1, _bin("%")),
}

UNARY_OPS: dict[TK, str] = {
    TK.MINUS: "-",
    TK.BANG: "!",
    TK.AINT: "!",
    TK.TILDE: "~",
}

ASSIGN_OPS: dict[TK, str] = {
    TK.EQ: "=",
    TK.PLUS_EQ: "+=",
    TK.MINUS_EQ: "-=",
    TK.STAR_EQ: "*=",
    TK.SLASH_EQ: "/=",
    TK.PERCENT_EQ: "%=",
    TK.STAR_STAR_EQ: "**=",
    TK.PIPE_PIPE_EQ: "||=",
}

STMT_START_KINDS: frozenset[TK] = frozenset(
    {
        TK.YO, TK.DEADASS, TK.BET, TK.SQUAD, TK.SUS, TK.BRUH, TK.GRIND,
        TK.BOUNCE, TK.BAIL, TK.NVM, TK.SKETCHY, TK.CHUCK, TK.GIMME, TK.FLEX,
        TK.YAP, TK.YEET, TK.MUMBLE, TK.VIBE,
    }
)

_STMT_END_KINDS = frozenset({TK.NEWLINE, TK.SEMICOLON, TK.RBRACE, TK.EOF})

MAX_ERRORS = 5


class Parser:
    def __init__(self, tokens: list[Token], source: SourceFile):
        self.tokens = tokens
        self.source = source
        self.pos = 0
        self.errors: list[ParserHadAStroke] = []

    # -- token stream primitives -----------------------------------------

    def peek(self, offset: int = 0) -> Token:
        i = min(self.pos + offset, len(self.tokens) - 1)
        return self.tokens[i]

    def check(self, kind: TK) -> bool:
        return self.peek().kind == kind

    def advance(self) -> Token:
        tok = self.tokens[self.pos]
        if tok.kind != TK.EOF:
            self.pos += 1
        return tok

    def match(self, kind: TK) -> Token | None:
        if self.check(kind):
            return self.advance()
        return None

    def expect(self, kind: TK, message: str) -> Token:
        if self.check(kind):
            return self.advance()
        raise self._error(self.peek().span, message)

    def _expect_name(self, message: str) -> Token:
        """Like `expect(TK.IDENT, ...)`, but also accepts any keyword token
        used as a name — property/method names specifically (`og.spawn(...)`,
        `bet same_energy(other) {}`), where the frozen keyword list (§3.3)
        collides with a magic method name (§3.7) or a squad's own
        constructor name. Keywords stay reserved everywhere else; this only
        widens "this position is unambiguously a name" grammar slots."""
        tok = self.peek()
        if tok.kind == TK.IDENT or tok.kind in _KEYWORD_KINDS:
            self.advance()
            return tok
        raise self._error(tok.span, message)

    def skip_newlines(self) -> None:
        while self.check(TK.NEWLINE):
            self.advance()

    def _skip_stmt_seps(self) -> None:
        while self.check(TK.NEWLINE) or self.check(TK.SEMICOLON):
            self.advance()

    def _span(self, start: Span, end: Span | None = None) -> Span:
        return start.merge(end) if end is not None else start

    def _error(self, span: Span, message: str, roast: str | None = None) -> ParserHadAStroke:
        return ParserHadAStroke(
            message,
            span=span,
            source=self.source,
            roast=roast or "i read this three times. it's still not code.",
        )

    def _check_reserved(self, tok: Token) -> None:
        if tok.text in RESERVED_FUTURE:
            raise self._error(
                tok.span,
                f"'{tok.text}' isn't implemented yet.",
                f"`{tok.text}` isn't a thing yet. i put it in the lexer to be aspirational.",
            )

    def _expect_stmt_end(self) -> None:
        if self.check(TK.NEWLINE) or self.check(TK.SEMICOLON):
            self.advance()
            return
        if self.check(TK.EOF) or self.check(TK.RBRACE):
            return
        raise self._error(self.peek().span, "expected end of statement (a newline or ';') here.")

    def _synchronize(self, end_kind: TK) -> None:
        """Skip to the next statement boundary, or to `end_kind` (the RBRACE of
        the enclosing block, or EOF at the top level) — whichever comes first.
        `end_kind` itself is left unconsumed for the caller to `expect()`."""
        while not self.check(TK.EOF) and not self.check(end_kind):
            if self.check(TK.NEWLINE) or self.check(TK.SEMICOLON):
                self.advance()
                return
            if self.peek().kind in STMT_START_KINDS:
                return
            self.advance()

    def _parse_statement_list(self, end_kind: TK) -> list:
        """Parse statements until `end_kind` (RBRACE for a block, EOF for the
        whole program), recovering from syntax errors one statement at a time
        so a single run can report more than one."""
        statements = []
        self._skip_stmt_seps()
        while not self.check(end_kind) and not self.check(TK.EOF):
            try:
                stmt = self.parse_statement()
                if stmt is not None:
                    statements.append(stmt)
            except ParserHadAStroke as exc:
                self.errors.append(exc)
                self._synchronize(end_kind)
            self._skip_stmt_seps()
        return statements

    # -- top level ----------------------------------------------------------

    def parse_program(self) -> Program:
        statements = self._parse_statement_list(TK.EOF)
        if self.errors:
            raise ParseErrorBundle(self.errors)
        span = self.source.make_span(0, len(self.source.text))
        return Program(tuple(statements), span)

    # -- statements -----------------------------------------------------

    def parse_statement(self):
        tok = self.peek()
        self._check_reserved(tok)
        dispatch = {
            TK.YO: self._parse_var_decl,
            TK.DEADASS: self._parse_const_decl,
            TK.BET: self._parse_func_decl,
            TK.SQUAD: self._parse_squad_decl,
            TK.SUS: self._parse_if,
            TK.BRUH: self._parse_while,
            TK.GRIND: self._parse_for,
            TK.BOUNCE: self._parse_return,
            TK.BAIL: self._parse_break,
            TK.NVM: self._parse_continue,
            TK.SKETCHY: self._parse_try,
            TK.CHUCK: self._parse_chuck,
            TK.GIMME: self._parse_import,
            TK.FLEX: self._parse_export,
            TK.YAP: lambda: self._parse_yap(True),
            TK.YEET: lambda: self._parse_yap(True),
            TK.MUMBLE: lambda: self._parse_yap(False),
            TK.VIBE: self._parse_vibe_stmt,
        }
        handler = dispatch.get(tok.kind)
        if handler is not None:
            return handler()
        return self._parse_expr_stmt()

    def _parse_block(self) -> Block:
        open_tok = self.expect(TK.LBRACE, "expected '{' to start a block here.")
        statements = []
        self._skip_stmt_seps()
        while not self.check(TK.RBRACE) and not self.check(TK.EOF):
            stmt = self.parse_statement()
            if stmt is not None:
                statements.append(stmt)
            self._skip_stmt_seps()
        close_tok = self.expect(TK.RBRACE, "this block never closes. it's missing a '}'.")
        return Block(tuple(statements), self._span(open_tok.span, close_tok.span))

    def _parse_var_decl(self) -> VarDecl:
        start = self.advance()  # yo
        name_tok = self.expect(TK.IDENT, "expected a variable name after 'yo'.")
        initializer = None
        if self.match(TK.EQ):
            initializer = self.parse_assignment()
        self._expect_stmt_end()
        return VarDecl(name_tok.text, initializer, self._span(start.span))

    def _parse_const_decl(self) -> ConstDecl:
        start = self.advance()  # deadass
        name_tok = self.expect(TK.IDENT, "expected a constant name after 'deadass'.")
        self.expect(
            TK.EQ,
            "'deadass' needs a value right away: deadass X = something. constants don't get to be lazy.",
        )
        initializer = self.parse_assignment()
        self._expect_stmt_end()
        return ConstDecl(name_tok.text, initializer, self._span(start.span))

    def _parse_param_list(self) -> tuple[list[Param], str | None]:
        params: list[Param] = []
        variadic: str | None = None
        seen_default = False
        self.skip_newlines()
        while not self.check(TK.RPAREN):
            if self.match(TK.ELLIPSIS):
                name_tok = self.expect(TK.IDENT, "expected a parameter name after '...'.")
                variadic = name_tok.text
                self.skip_newlines()
                if self.check(TK.COMMA):
                    raise self._error(
                        self.peek().span,
                        "'...rest' has to be the last parameter.",
                        "...rest has to be the last parameter. it's not sharing the spotlight.",
                    )
                break
            name_tok = self.expect(TK.IDENT, "expected a parameter name.")
            default = None
            if self.match(TK.EQ):
                self.skip_newlines()
                default = self.parse_assignment()
                seen_default = True
            elif seen_default:
                raise self._error(
                    name_tok.span,
                    "default parameters must come after all required ones.",
                    "default parameters have to go at the end. no take-backs.",
                )
            params.append(Param(name_tok.text, default, name_tok.span))
            self.skip_newlines()
            if not self.match(TK.COMMA):
                break
            self.skip_newlines()
        self.skip_newlines()
        return params, variadic

    def _parse_func_decl(self, allow_keyword_name: bool = False) -> FuncDecl:
        start = self.advance()  # bet
        # allow_keyword_name: squad methods only (§16) — a magic method name
        # like `same_energy` collides with a keyword; top-level/nested `bet`
        # keeps strict IDENT so `yo yap = ...`-style shadowing stays illegal.
        name_tok = (
            self._expect_name("expected a method name after 'bet'.")
            if allow_keyword_name
            else self.expect(TK.IDENT, "expected a function name after 'bet'.")
        )
        self.expect(TK.LPAREN, "expected '(' to start this function's parameter list.")
        params, variadic = self._parse_param_list()
        self.expect(TK.RPAREN, "this parameter list never closes. it's missing a ')'.")
        self.skip_newlines()
        body = self._parse_block()
        return FuncDecl(name_tok.text, tuple(params), variadic, body, self._span(start.span, body.span))

    def _parse_if(self) -> If:
        start = self.advance()  # sus
        self.expect(TK.LPAREN, "'sus' needs parens around its condition: sus (condition) { }.")
        self.skip_newlines()
        cond = self.parse_assignment()
        self.skip_newlines()
        self.expect(TK.RPAREN, "this condition never closes. it's missing a ')'.")
        self.skip_newlines()
        then_branch = self._parse_block()
        elif_branches: list[tuple] = []
        self.skip_newlines()
        while self.check(TK.KINDA_SUS):
            self.advance()
            self.expect(TK.LPAREN, "'kinda_sus' needs parens around its condition too.")
            self.skip_newlines()
            econd = self.parse_assignment()
            self.skip_newlines()
            self.expect(TK.RPAREN, "this condition never closes. it's missing a ')'.")
            self.skip_newlines()
            eblock = self._parse_block()
            elif_branches.append((econd, eblock))
            self.skip_newlines()
        else_branch = None
        if self.check(TK.NAH):
            self.advance()
            self.skip_newlines()
            else_branch = self._parse_block()
        end_span = else_branch.span if else_branch else (elif_branches[-1][1].span if elif_branches else then_branch.span)
        return If(cond, then_branch, tuple(elif_branches), else_branch, self._span(start.span, end_span))

    def _parse_while(self) -> While:
        start = self.advance()  # bruh
        self.expect(TK.LPAREN, "'bruh' needs parens around its condition: bruh (condition) { }.")
        self.skip_newlines()
        cond = self.parse_assignment()
        self.skip_newlines()
        self.expect(TK.RPAREN, "this condition never closes. it's missing a ')'.")
        self.skip_newlines()
        body = self._parse_block()
        return While(cond, body, self._span(start.span, body.span))

    def _parse_for(self):
        start = self.advance()  # grind
        var_tok = self.expect(TK.IDENT, "expected a loop variable name after 'grind'.")
        if self.match(TK.FROM):
            self.skip_newlines()
            start_e = self.parse_assignment()
            self.skip_newlines()
            self.expect(TK.TO, "range loops need 'to': grind i from 0 to 10 { }.")
            self.skip_newlines()
            stop_e = self.parse_assignment()
            step_e = None
            self.skip_newlines()
            if self.match(TK.STEP):
                self.skip_newlines()
                step_e = self.parse_assignment()
                self.skip_newlines()
            body = self._parse_block()
            return ForRange(var_tok.text, start_e, stop_e, step_e, body, self._span(start.span, body.span))
        if self.match(TK.IN):
            self.skip_newlines()
            iterable = self.parse_assignment()
            self.skip_newlines()
            body = self._parse_block()
            return ForEach(var_tok.text, iterable, body, self._span(start.span, body.span))
        raise self._error(
            self.peek().span,
            "a 'grind' loop needs 'from ... to ...' or 'in' after the loop variable.",
        )

    def _parse_return(self) -> Return:
        start = self.advance()  # bounce
        value = None
        if self.peek().kind not in _STMT_END_KINDS:
            value = self.parse_assignment()
        self._expect_stmt_end()
        return Return(value, self._span(start.span, value.span if value else start.span))

    def _parse_break(self) -> Break:
        start = self.advance()
        self._expect_stmt_end()
        return Break(start.span)

    def _parse_continue(self) -> Continue:
        start = self.advance()
        self._expect_stmt_end()
        return Continue(start.span)

    def _parse_try(self) -> Try:
        start = self.advance()  # sketchy
        self.skip_newlines()
        body = self._parse_block()
        catch_var = None
        catch_body = None
        finally_body = None
        self.skip_newlines()
        if self.check(TK.MY_BAD):
            self.advance()
            self.expect(TK.LPAREN, "'my_bad' needs the caught error in parens: my_bad (e) { }.")
            name_tok = self.expect(TK.IDENT, "expected a name for the caught error.")
            self.expect(TK.RPAREN, "this never closes. it's missing a ')'.")
            self.skip_newlines()
            catch_var = name_tok.text
            catch_body = self._parse_block()
            self.skip_newlines()
        if self.check(TK.REGARDLESS):
            self.advance()
            self.skip_newlines()
            finally_body = self._parse_block()
        if catch_body is None and finally_body is None:
            raise self._error(
                start.span,
                "a 'sketchy' block needs a 'my_bad' or a 'regardless'.",
                "a sketchy block needs a my_bad or a regardless. otherwise what's the point.",
            )
        end_span = (finally_body or catch_body).span
        return Try(body, catch_var, catch_body, finally_body, self._span(start.span, end_span))

    def _parse_chuck(self) -> Chuck:
        start = self.advance()  # chuck
        value = self.parse_assignment()
        self._expect_stmt_end()
        return Chuck(value, self._span(start.span, value.span))

    def _parse_import(self):
        start = self.advance()  # gimme
        if self.check(TK.LBRACE):
            self.advance()
            names = []
            self.skip_newlines()
            while not self.check(TK.RBRACE):
                name_tok = self.expect(TK.IDENT, "expected a name to import.")
                names.append(name_tok.text)
                self.skip_newlines()
                if not self.match(TK.COMMA):
                    break
                self.skip_newlines()
            self.skip_newlines()
            self.expect(TK.RBRACE, "this import list never closes. it's missing a '}'.")
            self.expect(TK.FROM, "named imports need 'from': gimme { a, b } from \"file.funny\".")
            path_tok = self.expect(TK.STRING, "expected a quoted module path after 'from'.")
            self._expect_stmt_end()
            return Import(path_tok.value, False, None, tuple(names), self._span(start.span, path_tok.span))
        if self.check(TK.STRING):
            path_tok = self.advance()
            alias = None
            if self.match(TK.AS):
                alias_tok = self.expect(TK.IDENT, "expected a name after 'as'.")
                alias = alias_tok.text
            self._expect_stmt_end()
            return Import(path_tok.value, False, alias, None, self._span(start.span, path_tok.span))
        # `sus` is both the reflection stdlib module (§7) and the `if`
        # keyword (§3.3) — a genuine name collision (PLAN.md §16). Since a
        # bare `gimme <name>` only ever means "stdlib module" (§3.8), SUS is
        # accepted here as a contextual keyword too, the same way
        # from/to/step/as/in already are elsewhere in the grammar.
        if self.check(TK.IDENT) or self.check(TK.SUS):
            name_tok = self.advance()
            alias = None
            if self.match(TK.AS):
                alias_tok = self.expect(TK.IDENT, "expected a name after 'as'.")
                alias = alias_tok.text
            self._expect_stmt_end()
            return Import(name_tok.text, True, alias, None, self._span(start.span, name_tok.span))
        raise self._error(
            self.peek().span,
            'gimme needs a quoted path, a stdlib module name, or { names } from "path".',
        )

    def _parse_export(self) -> Export:
        start = self.advance()  # flex
        if self.check(TK.BET):
            decl = self._parse_func_decl()
        elif self.check(TK.DEADASS):
            decl = self._parse_const_decl()
        elif self.check(TK.YO):
            decl = self._parse_var_decl()
        elif self.check(TK.SQUAD):
            decl = self._parse_squad_decl()
        else:
            raise self._error(
                self.peek().span,
                "'flex' needs something to flex: flex bet ..., flex deadass ..., flex yo ..., or flex squad ....",
            )
        return Export(decl, self._span(start.span, decl.span))

    def _parse_squad_decl(self) -> SquadDecl:
        start = self.advance()  # squad
        name_tok = self.expect(TK.IDENT, "expected a squad name after 'squad'.")
        superclass = None
        if self.match(TK.INHERITS):
            super_tok = self.expect(TK.IDENT, "expected a superclass name after 'inherits'.")
            superclass = super_tok.text
        self.skip_newlines()
        self.expect(TK.LBRACE, "expected '{' to start this squad's body.")
        self._skip_stmt_seps()
        spawn = None
        methods = []
        while not self.check(TK.RBRACE) and not self.check(TK.EOF):
            if self.check(TK.SPAWN):
                spawn_tok = self.advance()
                if spawn is not None:
                    raise self._error(spawn_tok.span, "a squad can only have one spawn. pick one.")
                self.expect(TK.LPAREN, "expected '(' after 'spawn'.")
                params, variadic = self._parse_param_list()
                self.expect(TK.RPAREN, "this parameter list never closes. it's missing a ')'.")
                self.skip_newlines()
                body = self._parse_block()
                spawn = FuncDecl("spawn", tuple(params), variadic, body, self._span(spawn_tok.span, body.span))
            elif self.check(TK.BET):
                methods.append(self._parse_func_decl(allow_keyword_name=True))
            else:
                raise self._error(
                    self.peek().span,
                    "a squad body only has 'spawn' and 'bet' methods in it.",
                )
            self._skip_stmt_seps()
        close_tok = self.expect(TK.RBRACE, "this squad never closes. it's missing a '}'.")
        return SquadDecl(name_tok.text, superclass, spawn, tuple(methods), self._span(start.span, close_tok.span))

    def _parse_yap(self, newline: bool) -> Yap:
        start = self.advance()  # yap / yeet / mumble
        args = []
        if self.peek().kind not in _STMT_END_KINDS:
            args.append(self.parse_assignment())
            while self.match(TK.COMMA):
                self.skip_newlines()
                args.append(self.parse_assignment())
        self._expect_stmt_end()
        end_span = args[-1].span if args else start.span
        return Yap(tuple(args), newline, self._span(start.span, end_span))

    def _parse_vibe_stmt(self) -> VibeStmt:
        start = self.advance()
        self._expect_stmt_end()
        return VibeStmt(start.span)

    def _parse_expr_stmt(self) -> ExprStmt:
        expr = self.parse_assignment()
        self._expect_stmt_end()
        return ExprStmt(expr, expr.span)

    # -- expressions: precedence climbing --------------------------------

    def parse_assignment(self):
        left = self.parse_ternary()
        op_kind = self.peek().kind
        if op_kind in ASSIGN_OPS:
            op_tok = self.advance()
            self.skip_newlines()
            value = self.parse_assignment()
            return self._make_assign(left, ASSIGN_OPS[op_kind], value, op_tok)
        return left

    def _make_assign(self, target, op: str, value, op_tok: Token):
        span = self._span(target.span, value.span)
        if isinstance(target, Identifier):
            return Assign(target, op, value, span)
        if isinstance(target, Get):
            return Set(target.obj, target.name, op, value, span)
        if isinstance(target, Index):
            return SetIndex(target.obj, target.index, op, value, span)
        raise self._error(
            target.span,
            "that's not something you can assign to.",
            "you can't assign to that. that's not a place.",
        )

    def parse_ternary(self):
        cond = self.parse_binary(_PIPE)
        if self.match(TK.QUESTION):
            self.skip_newlines()
            then_e = self.parse_assignment()
            self.skip_newlines()
            self.expect(TK.COLON, "a ternary needs ':' for its else branch: cond ? a : b.")
            self.skip_newlines()
            else_e = self.parse_ternary()
            return Ternary(cond, then_e, else_e, self._span(cond.span, else_e.span))
        return cond

    def parse_binary(self, min_bp: int):
        left = self.parse_power()
        while True:
            entry = BINOP_TABLE.get(self.peek().kind)
            if entry is None:
                break
            lbp, rbp, ctor = entry
            if lbp < min_bp:
                break
            self.advance()
            self.skip_newlines()
            right = self.parse_binary(rbp)
            left = ctor(left, right, self._span(left.span, right.span))
        return left

    def parse_power(self):
        base = self.parse_unary()
        if self.check(TK.STAR_STAR):
            self.advance()
            self.skip_newlines()
            exponent = self.parse_power()
            return Binary("**", base, exponent, self._span(base.span, exponent.span))
        return base

    def parse_unary(self):
        tok = self.peek()
        if tok.kind in UNARY_OPS:
            self.advance()
            operand = self.parse_unary()
            return Unary(UNARY_OPS[tok.kind], operand, self._span(tok.span, operand.span))
        return self.parse_postfix()

    def parse_postfix(self):
        expr = self.parse_primary()
        while True:
            if self.check(TK.LPAREN):
                expr = self._finish_call(expr)
            elif self.check(TK.LBRACKET):
                expr = self._finish_index(expr)
            elif self.check(TK.DOT):
                self.advance()
                name_tok = self._expect_name("expected a property name after '.'.")
                expr = Get(expr, name_tok.text, self._span(expr.span, name_tok.span))
            elif self.check(TK.QUESTION_DOT):
                self.advance()
                name_tok = self._expect_name("expected a property name after '?.'.")
                expr = SafeGet(expr, name_tok.text, self._span(expr.span, name_tok.span))
            else:
                break
        return expr

    def _finish_call(self, callee):
        self.advance()  # (
        self.skip_newlines()
        args = []
        while not self.check(TK.RPAREN):
            args.append(self.parse_assignment())
            self.skip_newlines()
            if not self.match(TK.COMMA):
                break
            self.skip_newlines()
        close_tok = self.expect(TK.RPAREN, "this call never closes. it's missing a ')'.")
        return Call(callee, tuple(args), self._span(callee.span, close_tok.span))

    def _finish_index(self, obj):
        self.advance()  # [
        self.skip_newlines()
        start_e = None
        if not self.check(TK.COLON) and not self.check(TK.RBRACKET):
            start_e = self.parse_assignment()
            self.skip_newlines()
        if self.match(TK.COLON):
            self.skip_newlines()
            stop_e = None
            if not self.check(TK.COLON) and not self.check(TK.RBRACKET):
                stop_e = self.parse_assignment()
                self.skip_newlines()
            step_e = None
            if self.match(TK.COLON):
                self.skip_newlines()
                if not self.check(TK.RBRACKET):
                    step_e = self.parse_assignment()
                    self.skip_newlines()
            close_tok = self.expect(TK.RBRACKET, "this slice never closes. it's missing a ']'.")
            return Slice(obj, start_e, stop_e, step_e, self._span(obj.span, close_tok.span))
        if start_e is None:
            raise self._error(self.peek().span, "empty [] isn't indexing anything.")
        close_tok = self.expect(TK.RBRACKET, "this index never closes. it's missing a ']'.")
        return Index(obj, start_e, self._span(obj.span, close_tok.span))

    def _parse_stash_lit(self) -> StashLit:
        open_tok = self.advance()  # [
        self.skip_newlines()
        elements = []
        while not self.check(TK.RBRACKET):
            elements.append(self.parse_assignment())
            self.skip_newlines()
            if not self.match(TK.COMMA):
                break
            self.skip_newlines()
        close_tok = self.expect(TK.RBRACKET, "this stash literal never closes. it's missing a ']'.")
        return StashLit(tuple(elements), self._span(open_tok.span, close_tok.span))

    def _parse_groupchat_lit(self) -> GroupChatLit:
        open_tok = self.advance()  # {
        self.skip_newlines()
        pairs = []
        while not self.check(TK.RBRACE):
            key = self.parse_assignment()
            self.skip_newlines()
            self.expect(TK.COLON, "a groupchat entry needs ':' between key and value.")
            self.skip_newlines()
            value = self.parse_assignment()
            pairs.append((key, value))
            self.skip_newlines()
            if not self.match(TK.COMMA):
                break
            self.skip_newlines()
        close_tok = self.expect(TK.RBRACE, "this groupchat literal never closes. it's missing a '}'.")
        return GroupChatLit(tuple(pairs), self._span(open_tok.span, close_tok.span))

    def _parse_lambda(self) -> Lambda:
        start = self.advance()  # lowkey
        self.expect(TK.LPAREN, "lambdas need parens around their params: lowkey (x) => x.")
        params, variadic = self._parse_param_list()
        self.expect(TK.RPAREN, "this parameter list never closes. it's missing a ')'.")
        if self.match(TK.ARROW):
            self.skip_newlines()
            # `=> { ... }` is a block body, same ambiguity JS has and resolves
            # the same way (PLAN.md §16) — the closures example in §3.5 relies
            # on this. Wrap in parens to return a groupchat literal directly:
            # `lowkey () => ({"a": 1})`.
            if self.check(TK.LBRACE):
                body = self._parse_block()
                return Lambda(tuple(params), variadic, body, False, self._span(start.span, body.span))
            body = self.parse_assignment()
            return Lambda(tuple(params), variadic, body, True, self._span(start.span, body.span))
        if self.check(TK.LBRACE):
            body = self._parse_block()
            return Lambda(tuple(params), variadic, body, False, self._span(start.span, body.span))
        raise self._error(
            self.peek().span,
            "a lambda needs '=>' or a '{ }' body.",
        )

    def _build_template(self, tok: Token) -> TemplateString:
        parts = []
        for kind, val in tok.value:
            if kind == "str":
                parts.append(("str", val))
            else:
                sub_parser = Parser(val, self.source)
                expr = sub_parser.parse_assignment()
                sub_parser.skip_newlines()
                if not sub_parser.check(TK.EOF):
                    raise self._error(
                        sub_parser.peek().span,
                        "this template substitution has leftover junk after the expression.",
                    )
                parts.append(("expr", expr))
        return TemplateString(tuple(parts), tok.span)

    def parse_primary(self):
        tok = self.peek()
        self._check_reserved(tok)
        if tok.kind in (TK.INT, TK.FLOAT, TK.STRING):
            self.advance()
            return Literal(tok.value, tok.span)
        if tok.kind == TK.TEMPLATE:
            self.advance()
            return self._build_template(tok)
        if tok.kind == TK.FAX:
            self.advance()
            return Literal(True, tok.span)
        if tok.kind == TK.CAP:
            self.advance()
            return Literal(False, tok.span)
        if tok.kind == TK.GHOST:
            self.advance()
            return Literal(None, tok.span)
        if tok.kind == TK.IDENT or tok.kind == TK.SUS:
            # SUS: see the contextual-keyword note in _parse_import — inside
            # an expression, `sus` can only mean the reflection module, never
            # the `if` keyword (that's only ever recognized at statement
            # start, before expression parsing is reached at all).
            self.advance()
            return Identifier(tok.text, tok.span)
        if tok.kind == TK.ME:
            self.advance()
            return Me(tok.span)
        if tok.kind == TK.OG:
            self.advance()
            return Og(tok.span)
        if tok.kind == TK.LPAREN:
            self.advance()
            self.skip_newlines()
            expr = self.parse_assignment()
            self.skip_newlines()
            self.expect(TK.RPAREN, "this group never closes. it's missing a ')'.")
            return expr
        if tok.kind == TK.LBRACKET:
            return self._parse_stash_lit()
        if tok.kind == TK.LBRACE:
            return self._parse_groupchat_lit()
        if tok.kind == TK.LOWKEY:
            return self._parse_lambda()
        raise self._error(tok.span, f"didn't expect to see '{tok.text}' here.")


def parse_source(source: SourceFile) -> Program:
    tokens = Lexer(source).tokenize()
    return Parser(tokens, source).parse_program()


def parse_expr(src: str):
    """Parse a single expression from raw text. Used by tests and templates."""
    source = SourceFile("<expr>", src)
    tokens = Lexer(source).tokenize()
    parser = Parser(tokens, source)
    expr = parser.parse_assignment()
    parser.skip_newlines()
    if not parser.check(TK.EOF):
        raise parser._error(parser.peek().span, "unexpected leftover tokens after this expression.")
    return expr
