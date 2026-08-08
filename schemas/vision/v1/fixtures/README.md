# Camera mount geometry fixtures

`camera_mount_geometry_v1.json` is a consumer fixture: apply its static matrix once to the optical
point, then apply exactly one matching dynamic `root_from_mount_matrix4x4`. The two dynamic poses
must produce the listed root points; reapplying the optical remap or static translation fails both
cases.

`pointcloud_pcd1_v2_camera_mount_expected.json` describes the PCD1 v2 golden point/matrix pair.
`provider_test_pcd1` serializes the same fixed values and rejects trailing or substituted bytes
without changing the version-2 layout.
