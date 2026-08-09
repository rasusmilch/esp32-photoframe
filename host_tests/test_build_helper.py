import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("photoframe_build", ROOT / "build.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class BuildHelperTests(unittest.TestCase):
    def invocation(
        self, environment=None, arguments=(), board="seeedstudio_reterminal_e1002"
    ):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = build.idf_invocation(
                board,
                list(arguments),
                environ={} if environment is None else environment,
            )
        return result, output.getvalue()

    def test_target_is_explicit_without_inherited_target(self):
        (base, defines, post, environment), output = self.invocation()
        self.assertEqual(base.count("-DIDF_TARGET=esp32s3"), 1)
        self.assertEqual(environment["IDF_TARGET"], "esp32s3")
        self.assertEqual(defines, [])
        self.assertEqual(post, [])
        self.assertEqual(output, "")

    def test_stale_inherited_target_is_overridden_and_diagnosed(self):
        (base, _, _, environment), output = self.invocation({"IDF_TARGET": "esp32"})
        self.assertIn("-DIDF_TARGET=esp32s3", base)
        self.assertEqual(environment["IDF_TARGET"], "esp32s3")
        self.assertIn("Ignoring inherited IDF_TARGET=esp32", output)

    def test_matching_inherited_target_has_no_warning(self):
        (_, _, _, environment), output = self.invocation({"IDF_TARGET": "esp32s3"})
        self.assertEqual(environment["IDF_TARGET"], "esp32s3")
        self.assertEqual(output, "")

    def test_conflicting_caller_target_is_removed_and_diagnosed(self):
        (base, defines, _, environment), output = self.invocation(
            arguments=("-DIDF_TARGET=esp32", "-DFIRMWARE_VERSION=test")
        )
        self.assertEqual(base.count("-DIDF_TARGET=esp32s3"), 1)
        self.assertNotIn("-DIDF_TARGET=esp32", defines)
        self.assertEqual(defines, ["-DFIRMWARE_VERSION=test"])
        self.assertEqual(environment["IDF_TARGET"], "esp32s3")
        self.assertIn("Ignoring caller IDF_TARGET=esp32", output)

    def test_board_defaults_debug_overlay_and_post_arguments_are_preserved(self):
        base, defines, post, _ = build.idf_invocation(
            "seeedstudio_reterminal_e1004",
            ["-DOPTION=value", "-p", "/dev/ttyUSB0", "flash", "monitor"],
            debug=True,
            environ={},
        )
        defaults = next(
            value for value in base if value.startswith("-DSDKCONFIG_DEFAULTS=")
        )
        self.assertIn(
            "boards/sdkconfig.defaults.seeedstudio_reterminal_e1004", defaults
        )
        self.assertTrue(defaults.endswith(";sdkconfig.defaults.debug"))
        self.assertEqual(defines, ["-DOPTION=value"])
        self.assertEqual(post, ["-p", "/dev/ttyUSB0", "flash", "monitor"])

    def test_all_supported_boards_use_project_target(self):
        self.assertEqual(len(build.BOARDS), 6)
        for board in build.BOARDS:
            with self.subTest(board=board):
                base, _, _, environment = build.idf_invocation(board, [], environ={})
                self.assertIn("-DIDF_TARGET=esp32s3", base)
                self.assertEqual(environment["IDF_TARGET"], "esp32s3")
                self.assertIn(f"boards/sdkconfig.defaults.{board}", " ".join(base))

    def test_fullclean_removes_only_generated_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for filename in ("sdkconfig", "partitions.csv", "keep.txt"):
                (root / filename).write_text(filename, encoding="utf-8")
            (root / "build").mkdir()
            (root / "build" / "artifact").write_text("generated", encoding="utf-8")
            (root / "unrelated").mkdir()

            build.clean_project_state(root)

            self.assertFalse((root / "sdkconfig").exists())
            self.assertFalse((root / "partitions.csv").exists())
            self.assertFalse((root / "build").exists())
            self.assertTrue((root / "keep.txt").exists())
            self.assertTrue((root / "unrelated").exists())

    def test_build_and_post_commands_share_deterministic_environment(self):
        calls = []

        def record(command, **kwargs):
            calls.append((command, kwargs))
            return mock.Mock(returncode=0)

        with mock.patch.dict(
            build.os.environ, {"IDF_TARGET": "esp32"}, clear=True
        ), mock.patch.object(build.subprocess, "run", side_effect=record):
            build.build_firmware(
                "seeedstudio_reterminal_e1002",
                ["-DOPTION=value", "-p", "/dev/ttyUSB0", "flash"],
            )

        self.assertEqual(len(calls), 2)
        build_command, build_kwargs = calls[0]
        post_command, post_kwargs = calls[1]
        self.assertIn("-DIDF_TARGET=esp32s3", build_command)
        self.assertIn("-DOPTION=value", build_command)
        self.assertEqual(build_command[-1], "build")
        self.assertEqual(post_command[-3:], ["-p", "/dev/ttyUSB0", "flash"])
        self.assertEqual(build_kwargs["env"]["IDF_TARGET"], "esp32s3")
        self.assertEqual(post_kwargs["env"]["IDF_TARGET"], "esp32s3")

    def test_firmware_after_fullclean_still_uses_project_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sdkconfig").write_text("stale", encoding="utf-8")
            (root / "partitions.csv").write_text("generated", encoding="utf-8")
            (root / "build").mkdir()
            calls = []

            def record(command, **kwargs):
                calls.append((command, kwargs))
                return mock.Mock(returncode=0)

            arguments = [
                "build.py",
                "--board",
                "seeedstudio_reterminal_e1002",
                "--step",
                "firmware",
                "--fullclean",
            ]
            with mock.patch.object(build, "PROJECT_ROOT", root), mock.patch.object(
                build.subprocess, "run", side_effect=record
            ), mock.patch.object(sys, "argv", arguments), mock.patch.dict(
                build.os.environ, {"IDF_TARGET": "esp32"}, clear=True
            ):
                build.main()

            self.assertFalse((root / "sdkconfig").exists())
            self.assertFalse((root / "partitions.csv").exists())
            self.assertFalse((root / "build").exists())
            self.assertEqual(len(calls), 1)
            self.assertIn("-DIDF_TARGET=esp32s3", calls[0][0])
            self.assertEqual(calls[0][1]["env"]["IDF_TARGET"], "esp32s3")


if __name__ == "__main__":
    unittest.main()
