<!--
  Copyright (c) 2022 Laczen

  SPDX-License-Identifier: Apache-2.0
-->
# KVS - key value store for embedded devices as a zephyr module 

The key value store library is also provided as a zephyr module for easy
integration in the zephyr build environment. To enable kvs as a zephyr module
the submanifest file (`zephyr/submanifests/example.yaml`) can be altered:

```
# Example manifest file you can include into the main west.yml file.
#
# To make this work, copy this file's contents to a new file,
# 'example.yaml', in the same directory.
#
# Then change the 'name' and 'url' below and run 'west update'.
#
# Your module will be added to the local workspace and kept in sync
# every time you run 'west update'.
#
# If you want to fetch a particular commit rather than the main
# branch, change the 'revision' line accordingly.

manifest:
  projects:
    - name: kvs
      url: https://github.com/Laczen/kvs
      revision: main
```

After the project has been added calling `west update` ads the kvs directory to
the zephyr workspace. If your workspace is called `zephyr_project` examples can 
be found under `zephyr_project/kvs/zephyr/samples`and tests under
`zephyr_project/kvs/zephyr/tests`.`
