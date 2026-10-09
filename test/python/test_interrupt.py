"""Embedded cancellation tests; pass GGSQL_EXTENSION_PATH to a built extension.

Run with: python -m unittest discover -s test/python -v
Each scenario runs in a subprocess so a regression cannot hang the test runner.
"""

import os
from pathlib import Path
import subprocess
import sys
import threading
import traceback
import unittest


def run_scenario(form, threads):
    import duckdb

    extension = Path(os.environ['GGSQL_EXTENSION_PATH']).resolve()
    con = duckdb.connect(config={'allow_unsigned_extensions': 'true', 'threads': threads})
    con.execute("LOAD '" + str(extension).replace("'", "''") + "'")
    con.execute("SET ggsql_output = 'spec'")
    started = threading.Event()

    def mark_started():
        started.set()
        return 0

    # Registered functions live in the shared catalog, so the sibling connection
    # can call this. The event proves we interrupt during inner SQL execution.
    con.create_function('ggsql_mark_started', mark_started, [], 'BIGINT', side_effects=True)
    query = (
        'SELECT sum(i + (SELECT ggsql_mark_started())) AS x, 1 AS y '
        'FROM range(1000000000000) t(i) VISUALISE x, y DRAW point'
    )
    if form == 'scalar':
        query = "SELECT ggsql('" + query + "')"

    # Keep unrelated work active on the same database while cancelling ggsql.
    other = con.cursor()
    other_started = threading.Event()
    other_errors = []

    def mark_other_started():
        other_started.set()
        return 0

    con.create_function('ggsql_other_started', mark_other_started, [], 'BIGINT', side_effects=True)

    def execute_other():
        try:
            other.execute(
                'WITH started AS MATERIALIZED (SELECT ggsql_other_started() AS marker) '
                'SELECT sum(i + marker) FROM range(1000000000000) t(i), started'
            ).fetchall()
        except Exception as exc:
            other_errors.append(exc)

    other_worker = threading.Thread(target=execute_other, daemon=True)
    other_worker.start()
    assert other_started.wait(10), f'unrelated query did not start: {other_errors}'

    for _ in range(3):
        started.clear()
        errors = []

        def execute():
            try:
                con.execute(query).fetchall()
            except Exception as exc:
                errors.append(exc)

        worker = threading.Thread(target=execute, daemon=True)
        worker.start()
        assert started.wait(10), f'inner query did not start: {errors}'
        con.interrupt()
        worker.join(5)
        assert not worker.is_alive(), 'outer interrupt did not stop the inner query within 5 seconds'
        assert len(errors) == 1 and isinstance(errors[0], duckdb.InterruptException), errors
        assert other_worker.is_alive() and not other_errors, 'interrupt reached an unrelated connection'

        # Cancellation must not poison the next invocation or its inner connection.
        assert con.execute('SELECT 42').fetchone() == (42,)
        spec = con.execute(
            "SELECT ggsql('WITH data AS (SELECT 1 AS x, 2 AS y) " "SELECT * FROM data VISUALISE x, y DRAW point')"
        ).fetchone()[0]
        assert 'vega-lite' in spec

    other.interrupt()
    other_worker.join(5)
    assert not other_worker.is_alive(), 'unrelated query failed to stop on its own interrupt'
    assert len(other_errors) == 1 and isinstance(other_errors[0], duckdb.InterruptException), other_errors
    other.close()

    # An ordinary inner SQL failure also needs to stop and join the forwarder.
    try:
        con.execute("SELECT ggsql('SELECT missing_column AS x VISUALISE x DRAW point')")
    except duckdb.InvalidInputException:
        pass
    else:
        raise AssertionError('expected the inner SQL binding error')
    assert con.execute('SELECT 42').fetchone() == (42,)
    con.close()


class InterruptTests(unittest.TestCase):
    def test_interrupt(self):
        for form in ('scalar', 'parser'):
            for threads in (1, 4):
                with self.subTest(form=form, threads=threads):
                    result = subprocess.run(
                        [sys.executable, __file__, form, str(threads)],
                        capture_output=True,
                        text=True,
                        timeout=60,
                        env={**os.environ, 'GGSQL_NO_OPEN_BROWSER': '1'},
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] in ('scalar', 'parser'):
        try:
            run_scenario(sys.argv[1], int(sys.argv[2]))
        except BaseException:
            traceback.print_exc()
            # A failing cancellation test may still have a blocked native worker.
            # Exit without waiting for Python/DuckDB shutdown to join that worker.
            sys.stdout.flush()
            sys.stderr.flush()
            os._exit(1)
    else:
        unittest.main()
