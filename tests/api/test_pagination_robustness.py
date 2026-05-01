"""Tests that malformed numeric query parameters do not crash the server.

Regression coverage for the safe_parse_int / safe_parse_int64 helpers: handlers
that previously called std::stoi unchecked would throw on bad input and crash
the request thread (500 or dropped connection). They should now fall back to
the default value (and still return 200) rather than error out.
"""


class TestPaginationRobustness:
    def test_notifications_bad_limit_does_not_500(self, client, admin_user):
        r = client.get(
            "/api/notifications?limit=abc",
            headers=admin_user["headers"],
        )
        # Before the fix this would 500 (uncaught std::invalid_argument).
        # Now the handler falls back to the default limit.
        assert r.status_code == 200, (
            f"Expected 200 with default limit; got {r.status_code} "
            f"({r.text!r})"
        )
        assert "notifications" in r.json()

    def test_notifications_bad_offset_does_not_500(self, client, admin_user):
        r = client.get(
            "/api/notifications?offset=xyz",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200, (
            f"Expected 200 with default offset; got {r.status_code} "
            f"({r.text!r})"
        )

    def test_notifications_trailing_garbage_limit(self, client, admin_user):
        r = client.get(
            "/api/notifications?limit=10abc",
            headers=admin_user["headers"],
        )
        # Trailing garbage should not crash — handler falls back to default.
        assert r.status_code == 200

    def test_notifications_huge_limit_clamped(self, client, admin_user):
        # Overflow on int32 — parser returns nullopt, handler falls back.
        r = client.get(
            "/api/notifications?limit=99999999999999999999",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200

    def test_search_bad_limit_does_not_500(self, client, admin_user):
        r = client.get(
            "/api/search?type=users&q=admin&limit=not-a-number",
            headers=admin_user["headers"],
        )
        assert r.status_code == 200, (
            f"Expected 200 with default limit; got {r.status_code} "
            f"({r.text!r})"
        )
