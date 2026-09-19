#!/usr/bin/env python3

"""Check that every selected repository revision runs on every target OS."""

import unittest

from ci_repo_apply_matrix import PROJECTS, RUNNERS, build_matrix


class RepoApplyMatrixTest(unittest.TestCase):
    def test_all_is_a_complete_cross_product(self):
        rows = build_matrix("all")["include"]
        self.assertEqual(len(rows), len(PROJECTS) * len(RUNNERS))
        for name, project in PROJECTS.items():
            by_project = [row for row in rows if row["project"] == name]
            self.assertEqual({row["os"] for row in by_project}, set(RUNNERS))
            self.assertEqual({row["revision"] for row in by_project}, {project["revision"]})

    def test_one_project_still_runs_on_all_operating_systems(self):
        rows = build_matrix("leveldb")["include"]
        self.assertEqual({row["project"] for row in rows}, {"leveldb"})
        self.assertEqual({row["os"] for row in rows}, set(RUNNERS))

    def test_unknown_project_is_rejected(self):
        with self.assertRaises(ValueError):
            build_matrix("unknown")


if __name__ == "__main__":
    unittest.main()
