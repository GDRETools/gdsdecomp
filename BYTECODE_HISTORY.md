## GDScript bytecode versions

### Branch 1.0

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [0b806ee](https://github.com/godotengine/godot/commit/0b806ee) | `2014.02.09` | 1                |                                                                 |
|                           | [8c1731b](https://github.com/godotengine/godot/commit/8c1731b) | `2014.02.15` | 2                | Added `load` function                                           |
|                           | [31ce3c5](https://github.com/godotengine/godot/commit/31ce3c5) | `2014.03.13` | 2                | Added `funcref` function                                        |
|                           | [703004f](https://github.com/godotengine/godot/commit/703004f) | `2014.06.16` | 2                | Added `hash` function                                           |
|                           | [8cab401](https://github.com/godotengine/godot/commit/8cab401) | `2014.09.15` | 2                | Added `YIELD` token                                             |
| **1.0.0**                 | [e82dc40](https://github.com/godotengine/godot/commit/e82dc40) | `2014.10.27` | 3                | Added `SETGET` token                                            |
| *1.1 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 1.1

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [2185c01](https://github.com/godotengine/godot/commit/2185c01) | `2015.02.15` | 3                | Added `var2str`, `str2var` functions                            |
|                           | [97f34a1](https://github.com/godotengine/godot/commit/97f34a1) | `2015.03.25` | 3                | Added `seed`, `get_inst` function                               |
|                           | [be46be7](https://github.com/godotengine/godot/commit/be46be7) | `2015.04.18` | 3                | Function `get_inst` renamed to `instance_from_id`               |
| **1.1.0**                 | [65d48d6](https://github.com/godotengine/godot/commit/65d48d6) | `2015.05.09` | 4                | Added `prints` function                                         |
| *2.0 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 2.0

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [48f1d02](https://github.com/godotengine/godot/commit/48f1d02) | `2015.06.24` | 5                | Added `SIGNAL` token                                            |
|                           | [30c1229](https://github.com/godotengine/godot/commit/30c1229) | `2015.12.28` | 6                | Added `ONREADY` token                                           |
|                           | [7d2d144](https://github.com/godotengine/godot/commit/7d2d144) | `2015.12.29` | 7                | Added `BREAKPOINT` token                                        |
|                           | [64872ca](https://github.com/godotengine/godot/commit/64872ca) | `2015.12.31` | 8                | Added `Color8` function                                         |
|                           | [6174585](https://github.com/godotengine/godot/commit/6174585) | `2016.01.02` | 9                | Added `CONST_PI` token                                          |
| **2.0.0 - 2.0.4-1**       | [23441ec](https://github.com/godotengine/godot/commit/23441ec) | `2016.01.02` | 10               | Added `var2bytes`, `bytes2var` functions                        |
| *2.1 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 2.1

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
| **2.1.0 - 2.1.1**         | [7124599](https://github.com/godotengine/godot/commit/7124599) | `2016.06.18` | 10               | Added `type_exists` function                                    |
| *3.0 branch diverge*      |                                                                |              |                  |                                                                 |
| **2.1.2**                 | [85585c7](https://github.com/godotengine/godot/commit/85585c7) | `2017.01.12` | 10               | Added `ColorN` function (backport from 3.0)                      |
| **2.1.3 - 2.1.6**         | [ed80f45](https://github.com/godotengine/godot/commit/ed80f45) | `2017.04.06` | 10               | Added `ENUM` token (backport from 3.0)                           |

### Branch 3.0

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [7124599](https://github.com/godotengine/godot/commit/7124599) | `2016.06.18` | 10               | Added `type_exists` function                                    |
|                           | [1add52b](https://github.com/godotengine/godot/commit/1add52b) | `2016.08.19` | 11               | Added `REMOTE`, `SYNC`, `MASTER`, `SLAVE` tokens                |
|                           | [4ee82a2](https://github.com/godotengine/godot/commit/4ee82a2) | `2016.08.27` | 11               | Added `ENUM` token                                              |
|                           | [513c026](https://github.com/godotengine/godot/commit/513c026) | `2016.10.03` | 11               | Added `char` function                                           |
|                           | [23381a5](https://github.com/godotengine/godot/commit/23381a5) | `2016.12.17` | 11               | Added `ColorN` function                                         |
|                           | [8b912d1](https://github.com/godotengine/godot/commit/8b912d1) | `2017.01.08` | 11               | Added `DOLLAR` token                                            |
|                           | [62273e5](https://github.com/godotengine/godot/commit/62273e5) | `2017.01.08` | 12               | Added `validate_json`, `parse_json`, `to_json` function         |
|                           | [f8a7c46](https://github.com/godotengine/godot/commit/f8a7c46) | `2017.01.11` | 12               | Added `MATCH` token                                             |
|                           | [c24c739](https://github.com/godotengine/godot/commit/c24c739) | `2017.01.20` | 12               | Added `WILDCARD` token                                          |
|                           | [5e938f0](https://github.com/godotengine/godot/commit/5e938f0) | `2017.02.28` | 12               | Added `CONST_INF`, `CONST_NAN` tokens                           |
|                           | [015d36d](https://github.com/godotengine/godot/commit/015d36d) | `2017.05.27` | 12               | Added `IS` token                                                |
|                           | [c6120e7](https://github.com/godotengine/godot/commit/c6120e7) | `2017.08.07` | 12               | Added `len` function                                            |
|                           | [d28da86](https://github.com/godotengine/godot/commit/d28da86) | `2017.08.18` | 12               | Added `inverse_lerp`, `range_lerp` functions                    |
|                           | [216a8aa](https://github.com/godotengine/godot/commit/216a8aa) | `2017.10.13` | 12               | Added `wrapi`, `wrapf` functions                                |
|                           | [91ca725](https://github.com/godotengine/godot/commit/91ca725) | `2017.11.12` | 12               | Added `CONST_TAU` token                                         |
| **3.0.0 - 3.0.6**         | [054a2ac](https://github.com/godotengine/godot/commit/054a2ac) | `2017.11.20` | 12               | Added `polar2cartesian`, `cartesian2polar` functions            |
| *3.1 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 3.1

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [ff1e7cf](https://github.com/godotengine/godot/commit/ff1e7cf) | `2018.05.07` | 12               | Added `is_instance_valid` function                              |
|                           | [a56d6ff](https://github.com/godotengine/godot/commit/a56d6ff) | `2018.05.17` | 12               | Added `get_stack` function                                      |
|                           | [3ea6d9f](https://github.com/godotengine/godot/commit/3ea6d9f) | `2018.05.28` | 12               | Added `print_debug` function                                    |
|                           | [8e35d93](https://github.com/godotengine/godot/commit/8e35d93) | `2018.05.29` | 12               | Added `REMOTESYNC`, `MASTERSYNC`, `SLAVESYNC` tokens            |
|                           | [a3f1ee5](https://github.com/godotengine/godot/commit/a3f1ee5) | `2018.07.15` | 13               | Added `CLASS_NAME` token                                        |
|                           | [8aab9a0](https://github.com/godotengine/godot/commit/8aab9a0) | `2018.07.20` | 13               | Added `AS`, `VOID`, `FORWARD_ARROW` tokens                      |
|                           | [d6b31da](https://github.com/godotengine/godot/commit/d6b31da) | `2018.09.15` | 13               | Added `PUPPET` token, token `SLAVESYNC` renamed to `PUPPETSYNC` |
|                           | [1ca61a3](https://github.com/godotengine/godot/commit/1ca61a3) | `2018.10.31` | 13               | Added `push_error`, `push_warning` function                     |
| **3.1**                   | [1a36141](https://github.com/godotengine/godot/commit/1a36141) | `2019.02.20` | 13               | Removed `DO`, `CASE`, `SWITCH` tokens                           |
| **3.1.1**                 | [514a3fb](https://github.com/godotengine/godot/commit/514a3fb) | `2019.03.19` | 13               | Added `smoothstep` function                                     |
| *3.2 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 3.2

| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
|                           | [7f7d97f](https://github.com/godotengine/godot/commit/7f7d97f) | `2019.04.29` | 13               | Added `is_equal_approx` and `is_zero_approx` functions          |
|                           | [620ec47](https://github.com/godotengine/godot/commit/620ec47) | `2019.05.01` | 13               | Added `step_decimals` function                                  |
|                           | [c00427a](https://github.com/godotengine/godot/commit/c00427a) | `2019.06.01` | 13               | Added `move_toward` function                                    |
|                           | [a60f242](https://github.com/godotengine/godot/commit/a60f242) | `2019.07.19` | 13               | Added `posmod` function                                         |
|                           | [6694c11](https://github.com/godotengine/godot/commit/6694c11) | `2019.07.20` | 13               | Added `lerp_angle` function                                     |
| **3.2**                   | [5565f55](https://github.com/godotengine/godot/commit/5565f55) | `2019.08.26` | 13               | Added `ord` function                                            |
| *4.0 branch diverge*      |                                                                |              |                  |                                                                 |
| *3.5 branch diverge*      |                                                                |              |                  |                                                                 |

### Branch 3.5
| Release                   | Commit                                                         | Date         | Bytecode         | Changes                                                         |
| ------------------------- | -------------------------------------------------------------- | ------------ | ---------------- | --------------------------------------------------------------- |
| **3.5**                   | [a7aad78](https://github.com/godotengine/godot/commit/a7aad78) | `2020.10.07` | 13               | added `deep_equal` function (never added to 4. x)               |

### Branch 4.0 (master)


| Release      | Commit                                                         | Date         | Bytecode | Changes                                     |
| ------------ | -------------------------------------------------------------- | ------------ | -------- | ------------------------------------------- |
|              | [506df14](https://github.com/godotengine/godot/commit/506df14) | `2020.02.12` | 13       | Removed `decimals` function                 |
|              | [f3f05dc](https://github.com/godotengine/godot/commit/f3f05dc) | `2020.02.13` | 13       | Removed `SYNC` and `SLAVE` tokens           |
| GDScript 2.0 | [5d6e853](https://github.com/godotengine/godot/commit/5d6e853) | `2020.07.24` | N/A      | **Compiled mode not implemented until 4.3** |
| **4.3**      | [77af6ca](https://github.com/godotengine/godot/commit/77af6ca) | `2024.02.09` | 100      | Added compiled mode                         |
|              | [b59d6be](https://github.com/godotengine/godot/commit/b59d6be) | `2025.05.18` | 100      | Added `ABSTRACT` token                      |
|              | [ee121ef](https://github.com/godotengine/godot/commit/ee121ef) | `2025.06.09` | 100      | Added `PERIOD_PERIOD_PERIOD` token          |
|              | [2e216b5](https://github.com/godotengine/godot/commit/2e216b5) | `2025.06.10` | 101      | Content header size changed                 |
