{
  'target_defaults': {
    'libraries': [
      '-lrootdev',
    ],
    'variables': {
      'deps': [
        'blkid',
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libmetrics-<(libbase_ver)',
        'libminijail',
        'libsession_manager-client',
        'libudev',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libdisks-adaptors',
      'type': 'none',
      'variables': {
        'dbus_service_config': 'dbus_bindings/dbus-service-config.json',
        'dbus_adaptors_out_dir': 'include/cros-disks/dbus_adaptors',
      },
      'sources': [
        'dbus_bindings/org.chromium.CrosDisks.xml',
      ],
      'includes': ['../common-mk/generate-dbus-adaptors.gypi'],
    },
    {
      'target_name': 'libdisks',
      'type': 'static_library',
      'dependencies': [
        'libdisks-adaptors',
      ],
      'sources': [
        'archive_manager.cc',
        'cros_disks_server.cc',
        'daemon.cc',
        'device_ejector.cc',
        'device_event.cc',
        'device_event_moderator.cc',
        'device_event_queue.cc',
        'disk.cc',
        'disk_manager.cc',
        'exfat_mounter.cc',
        'file_reader.cc',
        'filesystem.cc',
        'format_manager.cc',
        'fuse_helper.cc',
        'fuse_mount_manager.cc',
        'fuse_mounter.cc',
        'metrics.cc',
        'mount_info.cc',
        'mount_manager.cc',
        'mount_options.cc',
        'mounter.cc',
        'ntfs_mounter.cc',
        'platform.cc',
        'process.cc',
        'rename_manager.cc',
        'sandboxed_process.cc',
        'session_manager_proxy.cc',
        'sshfs_helper.cc',
        'system_mounter.cc',
        'udev_device.cc',
        'uri.cc',
        'usb_device_info.cc',
      ],
    },
    {
      'target_name': 'disks',
      'type': 'executable',
      'dependencies': ['libdisks'],
      'sources': [
        'main.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'disks_testrunner',
          'type': 'executable',
          'dependencies': [
            'libdisks',
            '../common-mk/testrunner.gyp:testrunner',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'archive_manager_unittest.cc',
            'device_event_moderator_unittest.cc',
            'device_event_queue_unittest.cc',
            'disk_manager_unittest.cc',
            'disk_unittest.cc',
            'file_reader_unittest.cc',
            'format_manager_unittest.cc',
            'fuse_helper_unittest.cc',
            'fuse_mount_manager_unittest.cc',
            'metrics_unittest.cc',
            'mount_info_unittest.cc',
            'mount_manager_unittest.cc',
            'mount_options_unittest.cc',
            'mounter_unittest.cc',
            'platform_unittest.cc',
            'process_unittest.cc',
            'rename_manager_unittest.cc',
            'sshfs_helper_unittest.cc',
            'system_mounter_unittest.cc',
            'udev_device_unittest.cc',
            'usb_device_info_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
