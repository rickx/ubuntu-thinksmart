-- ThinkSmart View: the front camera is exposed through libcamera
-- (S5KC505A + qcom-camss + SoftISP). Hide the raw CAMSS/Venus V4L2
-- nodes so applications only see the processed libcamera camera.
table.insert(v4l2_monitor.rules, {
  matches = {
    { { "device.product.name", "matches", "Qualcomm Camera Subsystem" } },
    { { "device.product.name", "matches", "Qualcomm Venus*" } },
  },
  apply_properties = { ["device.disabled"] = true },
})
