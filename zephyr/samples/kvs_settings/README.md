<!--
  Copyright (c) 2022 Laczen

  SPDX-License-Identifier: Apache-2.0
-->
# Using kvs as a backend for zephyr settings subsystem.

The sample illustrates how kvs can be used as a backend for settings. The sample
provides a shell interface (using e.g. minicom) to settings that can be read,
written, modified and listed.

```
uart:~$ settings write /test a -> write a (0xa) to /test
uart:~$ settings list -> list the available settings
uart:~$ settings read /test -> read the value of /test
uart:~$ settings list /sub -> list the available settings under /sub
```
