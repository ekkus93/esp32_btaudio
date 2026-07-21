#!/usr/bin/env python3
"""Static check (FIX3 §5.4): every POST/PUT/DELETE route in web_ui.c must
dispatch through route_dispatch() with a web_route_ctx_t whose
auth_required is true. No live httpd server is spun up here — esp_http_server
isn't mocked in this host-test harness — so this enumerates the route table
as C source text instead, the same way ci_check_main_layering.sh in the
sibling esp_bt_audio_source project statically checks main.c's structure.

Run: python3 -m pytest -q tools/test_web_ui_route_auth.py   (from esp_i2s_source/)
"""
import re
from pathlib import Path

WEB_UI_C = Path(__file__).resolve().parent.parent / "components" / "web_ui" / "web_ui.c"

MUTATING_METHODS = {"HTTP_POST", "HTTP_PUT", "HTTP_DELETE"}


def _parse_uri_entries(text):
    """Yield (var_name, method, handler, user_ctx_or_None) for every
    `const httpd_uri_t <name> = { ... };` block."""
    pattern = re.compile(
        r"const httpd_uri_t (\w+) = \{(.*?)\};", re.DOTALL
    )
    for m in pattern.finditer(text):
        name, body = m.group(1), m.group(2)
        method_m = re.search(r"\.method\s*=\s*(HTTP_\w+)", body)
        handler_m = re.search(r"\.handler\s*=\s*(\w+)", body)
        ctx_m = re.search(r"\.user_ctx\s*=\s*\(void \*\)&(\w+)", body)
        assert method_m and handler_m, f"malformed httpd_uri_t entry: {name}"
        yield name, method_m.group(1), handler_m.group(1), (
            ctx_m.group(1) if ctx_m else None
        )


def _parse_route_contexts(text):
    """Return {context_name: auth_required_bool} for every
    `static const web_route_ctx_t S_XXX = { ... };` definition."""
    pattern = re.compile(
        r"static const web_route_ctx_t (\w+)\s*=\s*\{(.*?)\};", re.DOTALL
    )
    out = {}
    for m in pattern.finditer(text):
        name, body = m.group(1), m.group(2)
        auth_m = re.search(r"\.auth_required\s*=\s*(true|false)", body)
        assert auth_m, f"web_route_ctx_t {name} missing .auth_required"
        out[name] = auth_m.group(1) == "true"
    return out


def _load():
    text = WEB_UI_C.read_text()
    entries = list(_parse_uri_entries(text))
    contexts = _parse_route_contexts(text)
    assert entries, f"no httpd_uri_t entries found in {WEB_UI_C}"
    assert contexts, f"no web_route_ctx_t definitions found in {WEB_UI_C}"
    return entries, contexts


def test_every_mutating_route_dispatches_through_auth_gate():
    entries, contexts = _load()
    mutating = [e for e in entries if e[1] in MUTATING_METHODS]
    assert mutating, "expected at least one POST/PUT/DELETE route"
    for name, method, handler, ctx_name in mutating:
        assert handler == "route_dispatch", (
            f"{name} ({method}) handler is '{handler}', not route_dispatch — "
            "a mutating route must not bypass the auth gate"
        )
        assert ctx_name is not None, (
            f"{name} ({method}) uses route_dispatch but has no user_ctx"
        )
        assert ctx_name in contexts, (
            f"{name} references undefined web_route_ctx_t '{ctx_name}'"
        )
        assert contexts[ctx_name], (
            f"{name} ({method}) -> {ctx_name} has auth_required=false"
        )


def test_get_routes_do_not_need_the_dispatcher():
    """Not a security requirement — just confirms the parser and current
    design agree: GET routes call their handler directly (no auth gate),
    matching FIX3 §5.4 ('GET endpoints MAY remain unauthenticated')."""
    entries, _ = _load()
    get_routes = [e for e in entries if e[1] == "HTTP_GET"]
    assert get_routes, "expected at least one GET route"
    for name, _method, handler, _ctx in get_routes:
        assert handler != "route_dispatch", (
            f"{name} is GET but routed through route_dispatch — update this "
            "test if a GET route now needs auth (it must still set "
            "auth_required=true, this assertion just needs updating)"
        )


def test_console_route_is_protected():
    """/api/console forwards raw commands to the WROOM32 (SEC-001) — must
    never be an exception to the auth gate."""
    entries, contexts = _load()
    console = [e for e in entries if "console" in e[0].lower()]
    assert console, "expected a console httpd_uri_t entry"
    for name, method, handler, ctx_name in console:
        assert handler == "route_dispatch"
        assert contexts[ctx_name] is True


if __name__ == "__main__":
    test_every_mutating_route_dispatches_through_auth_gate()
    test_get_routes_do_not_need_the_dispatcher()
    test_console_route_is_protected()
    print("OK")
