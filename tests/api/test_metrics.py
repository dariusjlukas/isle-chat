"""Black-box tests for the /metrics Prometheus endpoint.

The backend exposes a roll-your-own Prometheus exposition endpoint that
the deployment's nginx then ACLs to internal networks. The backend itself
does not authenticate, so these tests can hit it directly.
"""


class TestMetrics:
    def test_metrics_endpoint_returns_text_plain(self, client):
        # Drive at least one request so the labeled counter has content.
        client.get("/api/health")
        r = client.get("/metrics")
        assert r.status_code == 200, f"Unexpected status: {r.status_code} ({r.text!r})"
        ctype = r.headers.get("content-type", "")
        assert ctype.startswith("text/plain"), f"Unexpected Content-Type: {ctype}"

    def test_metrics_contains_expected_families(self, client):
        client.get("/api/health")
        r = client.get("/metrics")
        body = r.text

        # HELP/TYPE markers for the families we care about.
        assert "# TYPE enclave_http_requests_total counter" in body
        assert (
            "# TYPE enclave_http_request_duration_seconds histogram" in body
        ), body[:500]
        assert "# TYPE enclave_ws_connected_clients gauge" in body
        assert "# TYPE enclave_db_pool_size gauge" in body
        assert "# TYPE enclave_db_pool_in_use gauge" in body

        # Histogram +Inf bucket sentinel for /api/health (instrumented route).
        assert (
            'enclave_http_request_duration_seconds_bucket'
            '{route="/api/health",le="+Inf"}'
        ) in body

        # Counter row for the /api/health hit we just made.
        assert (
            'enclave_http_requests_total'
            '{method="GET",route="/api/health",status="200"}'
        ) in body

    def test_metrics_db_pool_size_is_positive(self, client):
        # The conftest sets DB_POOL_SIZE=2 for managed servers, but
        # externally-managed servers may run with the default. Either way the
        # value should be a positive integer.
        r = client.get("/metrics")
        body = r.text

        # Find the unlabeled gauge line.
        for line in body.splitlines():
            if line.startswith("enclave_db_pool_size "):
                value = int(line.split()[1])
                assert value >= 1, f"expected positive pool size, got {value}"
                return
        raise AssertionError("enclave_db_pool_size not present in /metrics output")

    def test_metrics_counter_increments_across_scrapes(self, client):
        # Scrape once to capture baseline.
        client.get("/api/health")
        body1 = client.get("/metrics").text

        # Generate additional health hits.
        for _ in range(3):
            client.get("/api/health")

        body2 = client.get("/metrics").text

        def extract_count(body: str) -> int:
            prefix = (
                'enclave_http_requests_total'
                '{method="GET",route="/api/health",status="200"} '
            )
            for line in body.splitlines():
                if line.startswith(prefix):
                    return int(line[len(prefix):].split()[0])
            raise AssertionError("counter row not found in /metrics output")

        before = extract_count(body1)
        after = extract_count(body2)
        assert after >= before + 3, f"counter did not advance: {before} -> {after}"
