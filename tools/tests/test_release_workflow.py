"""Exercise the workflow's actual Bash version-selection step without publishing."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


class ReleaseWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workflow = Path(__file__).resolve().parents[2] / ".github/workflows/release.yml"
        selection = workflow.read_text().split("id: version\n", 1)[1]
        cls.script = textwrap.dedent(re.search(r"        run: \|\n((?:          .*\n|\n)+)", selection)[1])

    def select(self, subject, requested="", event="push"):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            git = root / "git"
            git.write_text('#!/bin/sh\nprintf "%s\\n" "$COMMIT_SUBJECT"\n')
            git.chmod(0o755)
            output = root / "output"
            result = subprocess.run(
                ["bash", "-e", "-c", self.script],
                env={**os.environ, "PATH": f"{root}:{os.environ['PATH']}",
                     "COMMIT_SUBJECT": subject, "REQUESTED_VERSION": requested,
                     "EVENT_NAME": event, "GITHUB_OUTPUT": str(output)},
                capture_output=True, text=True,
            )
            values = dict(line.split("=", 1) for line in output.read_text().splitlines()) if output.exists() else {}
            return result, values

    def test_exact_version_subjects(self):
        for tag in ("v0.213", "v0.301", "v1.2.3"):
            with self.subTest(tag=tag):
                result, values = self.select(tag)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(values, {"release": "true", "tag": tag, "version": tag[1:]})

    def test_descriptive_subject_from_missed_release(self):
        result, values = self.select("SpinCoater v0.300: PIN auth, safety supervisor, watchdog, OTA hardening")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(values["tag"], "v0.300")
        self.assertEqual(values["version"], "0.300")

    def test_unrelated_commits_do_not_publish(self):
        for subject in ("Fix ramp restart race", "Update to v0.301", "v0.301oops", "SpinCoater v0.301oops"):
            with self.subTest(subject=subject):
                result, values = self.select(subject)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(values, {"release": "false"})

    def test_manual_release_from_fix_commit(self):
        result, values = self.select("Fix ramp restart race", "v0.301", "workflow_dispatch")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(values, {"release": "true", "tag": "v0.301", "version": "0.301"})

    def test_invalid_manual_versions_fail(self):
        for version in ("", "0.301", "v0.301; echo bad", "v0.301: description"):
            with self.subTest(version=version):
                result, values = self.select("v0.301", version, "workflow_dispatch")
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(values, {})


if __name__ == "__main__":
    unittest.main()
