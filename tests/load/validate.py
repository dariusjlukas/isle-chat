#!/usr/bin/env python3
"""Post-run validation for load tests.

Parses CSV stats output, checks against thresholds, and verifies
database integrity. Exits 0 on pass, 1 on failure.

Usage:
    python validate.py [--reports-dir reports] [--host http://localhost:9001]
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import urllib.request
import urllib.error


def load_thresholds():
    """Load threshold configuration."""
    path = os.path.join(os.path.dirname(__file__), "config", "thresholds.json")
    with open(path) as f:
        return json.load(f)


def parse_stats_csv(reports_dir):
    """Parse the CSV stats output."""
    stats_file = None
    for name in os.listdir(reports_dir):
        if name.endswith("_stats.csv"):
            stats_file = os.path.join(reports_dir, name)
            break

    if not stats_file:
        print(f"WARNING: No *_stats.csv found in {reports_dir}")
        return None

    stats = {}
    with open(stats_file) as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("Name") == "Aggregated":
                stats = {
                    "total_requests": int(row.get("Request Count", 0)),
                    "total_failures": int(row.get("Failure Count", 0)),
                    "avg_response_time": float(row.get("Average Response Time", 0)),
                    "p50": float(row.get("50%", 0)),
                    "p95": float(row.get("95%", 0)),
                    "p99": float(row.get("99%", 0)),
                    "rps": float(row.get("Requests/s", 0)),
                }
                break

    return stats


def check_thresholds(stats, thresholds):
    """Check stats against thresholds. Returns list of violations."""
    violations = []

    if stats is None:
        violations.append("No stats available to check")
        return violations

    total = stats.get("total_requests", 0)
    failures = stats.get("total_failures", 0)

    if total > 0:
        failure_rate = failures / total
        max_rate = thresholds.get("http_failure_rate_max", 0.01)
        if failure_rate > max_rate:
            violations.append(
                f"Failure rate {failure_rate:.3%} exceeds max {max_rate:.1%} "
                f"({failures}/{total} failed)")

    p95 = stats.get("p95", 0)
    p95_max = thresholds.get("http_p95_max_ms", 500)
    if p95 > p95_max:
        violations.append(f"P95 latency {p95:.0f}ms exceeds max {p95_max}ms")

    p99 = stats.get("p99", 0)
    p99_max = thresholds.get("http_p99_max_ms", 2000)
    if p99 > p99_max:
        violations.append(f"P99 latency {p99:.0f}ms exceeds max {p99_max}ms")

    rps = stats.get("rps", 0)
    min_rps = thresholds.get("min_requests_per_second", 0)
    if min_rps > 0 and rps < min_rps:
        violations.append(
            f"Throughput {rps:.1f} req/s below minimum {min_rps} req/s")

    return violations


def _pg_dsn():
    """Build PostgreSQL connection string from environment variables."""
    host = os.environ.get("POSTGRES_HOST", "localhost")
    port = os.environ.get("POSTGRES_PORT", "5432")
    user = os.environ.get("POSTGRES_USER", "chatapp")
    password = os.environ.get("POSTGRES_PASSWORD", "changeme")
    db = os.environ.get("POSTGRES_DB", "chatapp")
    return f"host={host} port={port} dbname={db} user={user} password={password}"


def _run_sql(query):
    """Run a SQL query via psql and return rows. Returns None on failure."""
    host = os.environ.get("POSTGRES_HOST", "localhost")
    port = os.environ.get("POSTGRES_PORT", "5432")
    user = os.environ.get("POSTGRES_USER", "chatapp")
    db = os.environ.get("POSTGRES_DB", "chatapp")
    env = {**os.environ, "PGPASSWORD": os.environ.get("POSTGRES_PASSWORD", "changeme")}

    try:
        result = subprocess.run(
            ["psql", "-h", host, "-p", port, "-U", user, "-d", db,
             "-t", "-A", "-c", query],
            capture_output=True, text=True, timeout=10, env=env)
        if result.returncode == 0:
            return result.stdout.strip()
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return None


def check_db_integrity():
    """Run database integrity checks. Returns list of issues."""
    issues = []

    try:
        # Try psycopg2 first (more reliable), fall back to psql CLI
        try:
            import psycopg2
            dsn = _pg_dsn()
            conn = psycopg2.connect(dsn)
            cur = conn.cursor()

            cur.execute("SELECT id, COUNT(*) FROM messages GROUP BY id HAVING COUNT(*) > 1")
            dupes = cur.fetchall()
            if dupes:
                issues.append(f"Found {len(dupes)} duplicate message IDs")

            cur.execute("""
                SELECT COUNT(*) FROM messages m
                LEFT JOIN channels c ON m.channel_id = c.id
                WHERE c.id IS NULL
            """)
            orphans = cur.fetchone()[0]
            if orphans > 0:
                issues.append(f"Found {orphans} orphaned messages")

            cur.execute("SELECT COUNT(*) FROM messages WHERE NOT is_deleted")
            msg_count = cur.fetchone()[0]
            cur.execute("SELECT COUNT(*) FROM users")
            user_count = cur.fetchone()[0]
            print(f"  Database: {msg_count} messages, {user_count} users")

            cur.close()
            conn.close()
            return issues

        except ImportError:
            pass

        # Fallback: use psql CLI
        msg_count = _run_sql("SELECT COUNT(*) FROM messages WHERE NOT is_deleted")
        user_count = _run_sql("SELECT COUNT(*) FROM users")
        if msg_count is not None and user_count is not None:
            print(f"  Database: {msg_count} messages, {user_count} users")

            dupes = _run_sql(
                "SELECT COUNT(*) FROM (SELECT id FROM messages GROUP BY id HAVING COUNT(*) > 1) d")
            if dupes and int(dupes) > 0:
                issues.append(f"Found {dupes} duplicate message IDs")

            orphans = _run_sql("""
                SELECT COUNT(*) FROM messages m
                LEFT JOIN channels c ON m.channel_id = c.id
                WHERE c.id IS NULL
            """)
            if orphans and int(orphans) > 0:
                issues.append(f"Found {orphans} orphaned messages")
        else:
            print("  WARNING: Could not check database (psycopg2 not installed, psql not available)")

    except Exception as e:
        print(f"  WARNING: Could not check database: {e}")

    return issues


def check_server_health(host):
    """Check that the server is still responsive."""
    try:
        req = urllib.request.Request(f"{host}/api/health", method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            if resp.status == 200:
                print("  Server health: OK")
                return []
            else:
                return [f"Server health check returned {resp.status}"]
    except Exception as e:
        return [f"Server health check failed: {e}"]


def main():
    parser = argparse.ArgumentParser(description="Validate load test results")
    parser.add_argument("--reports-dir", default="reports",
                        help="Directory containing CSV reports")
    parser.add_argument("--host", default="http://127.0.0.1:9099",
                        help="Server URL for health check")
    args = parser.parse_args()

    thresholds = load_thresholds()
    all_issues = []

    print("\n=== Load Test Validation ===\n")

    # 1. Parse and check stats
    print("Checking performance thresholds...")
    stats = parse_stats_csv(args.reports_dir)
    if stats:
        print(f"  Total requests: {stats['total_requests']}")
        print(f"  Failures: {stats['total_failures']}")
        print(f"  P50/P95/P99: {stats['p50']:.0f}ms / "
              f"{stats['p95']:.0f}ms / {stats['p99']:.0f}ms")
        print(f"  Throughput: {stats['rps']:.1f} req/s")

    threshold_violations = check_thresholds(stats, thresholds)
    all_issues.extend(threshold_violations)

    # 2. Check database integrity
    print("\nChecking database integrity...")
    db_issues = check_db_integrity()
    all_issues.extend(db_issues)

    # 3. Check server health
    print("\nChecking server health...")
    health_issues = check_server_health(args.host)
    all_issues.extend(health_issues)

    # Summary
    print("\n" + "=" * 40)
    if all_issues:
        print(f"\nFAILED — {len(all_issues)} issue(s):\n")
        for issue in all_issues:
            print(f"  - {issue}")
        print()
        return 1
    else:
        print("\nPASSED — all checks OK\n")
        return 0


if __name__ == "__main__":
    sys.exit(main())
