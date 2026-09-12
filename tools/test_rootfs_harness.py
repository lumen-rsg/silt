#!/usr/bin/env python3
"""Host regressions for UART acceptance framing; no QEMU required."""

import unittest
from types import SimpleNamespace

from test_rootfs import command_output
from dash_job_cases import frame_command
from dash_pipeline_cases import drive_terminal


class Session:
    def __init__(self, chunks):
        self.output = b""
        self.chunks = iter(chunks)
        self.sent = []

    def send(self, command):
        self.sent.append(command)

    def read_until(self, marker, timeout, start_offset=0):
        while marker not in self.output[start_offset:]:
            chunk = next(self.chunks, None)
            if chunk is None:
                return False
            self.output += chunk
        return True


class FramingTest(unittest.TestCase):
    runner = SimpleNamespace(PROMPT=b"1000$ ")

    def test_interactive_line_limit_includes_completion_marker(self):
        suffix = len(frame_command("", "DONE"))
        self.assertEqual(len(frame_command("x" * (128 - suffix), "DONE")), 128)
        with self.assertRaises(ValueError):
            frame_command("x" * (129 - suffix), "DONE")
        with self.assertRaises(ValueError):
            frame_command("echo one\necho two", "DONE")
        self.assertIn(" & printf", frame_command("true &", "DONE"))

    def test_pipeline_phases_do_not_reuse_old_acknowledgements(self):
        transcript = b"A\nB\nB\nA\n"
        seen = []
        sent = []

        def expect(marker, start):
            end = transcript.index(marker, start) + len(marker)
            seen.append(end)
            return end

        drive_terminal(expect, sent.append, 0,
                       [([b"A\n", b"B\n"], b"stop"),
                        ([b"A\n", b"B\n"], b"interrupt")])
        self.assertEqual(seen, [2, 4, 8, 6])
        self.assertEqual(sent, [b"stop", b"interrupt"])

    def test_pipeline_missing_member_prevents_signal(self):
        sent = []

        def expect(marker, start):
            if marker == b"B":
                raise TimeoutError("second member never became ready")
            return start + 1

        with self.assertRaises(TimeoutError):
            drive_terminal(expect, sent.append, 0, [([b"A", b"B"], b"interrupt")])
        self.assertEqual(sent, [])

    def test_echo_is_not_a_result(self):
        session = Session([b"echo PASS\n1000$ "])
        with self.assertRaises(TimeoutError):
            command_output(session, self.runner, "echo PASS", (b"PASS",))

    def test_truncated_echo_fails(self):
        session = Session([b"echo PASS\n1000$ "])
        with self.assertRaises(TimeoutError):
            command_output(session, self.runner, "echo PASS extra", (b"PASS",))

    def test_early_prompt_and_late_service_output(self):
        session = Session([b"1000$ echo PASS\n", b"1000$ ", b"PASS\n"])
        output = command_output(session, self.runner, "echo PASS", (b"PASS",))
        self.assertIn(b"PASS", output)
        self.assertNotIn(b"echo PASS", output)

    def test_input_limit_before_send(self):
        for command in ("x" * 127, "a\nb"):
            session = Session([])
            with self.assertRaises(ValueError):
                command_output(session, self.runner, command, ())
            self.assertEqual(session.sent, [])


if __name__ == "__main__":
    unittest.main()
