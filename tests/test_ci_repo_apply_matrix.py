#!/usr/bin/env python3

"""Check that native coverage and unsupported cells are both explicit."""

import unittest

from ci_repo_apply_matrix import PROJECTS, RUNNERS, build_matrix


class RepoApplyMatrixTest(unittest.TestCase):
    def test_every_project_os_cell_is_accounted_for(self):
        plan = build_matrix("all")
        supported = {(row["project"], row["os"]) for row in plan["include"]}
        unsupported = {(row["project"], row["os"]) for row in plan["unsupported"]}
        self.assertFalse(supported & unsupported)
        self.assertEqual(supported | unsupported,
                         {(project, runner) for project in PROJECTS for runner in RUNNERS})
        self.assertEqual(len(PROJECTS), 15)
        for row in plan["include"]:
            self.assertEqual(row["revision"], PROJECTS[row["project"]]["revision"])
        for row in plan["unsupported"]:
            self.assertTrue(row["reason"])

    def test_one_project_still_runs_on_all_operating_systems(self):
        rows = build_matrix("leveldb")["include"]
        self.assertEqual({row["project"] for row in rows}, {"leveldb"})
        self.assertEqual({row["os"] for row in rows}, set(RUNNERS))

    def test_non_cmake_projects_are_reported_not_silently_dropped(self):
        for name in ("redis", "weston"):
            plan = build_matrix(name)
            self.assertFalse(plan["include"])
            self.assertEqual({row["os"] for row in plan["unsupported"]}, set(RUNNERS))

    def test_c_projects_are_covered_on_three_native_runners(self):
        for name in ("mimalloc", "zstd", "libjpeg-turbo", "glfw", "curl", "zlib", "libpng"):
            self.assertEqual(len(build_matrix(name)["include"]), 3)

    def test_unknown_project_is_rejected(self):
        with self.assertRaises(ValueError):
            build_matrix("unknown")


if __name__ == "__main__":
    unittest.main()
