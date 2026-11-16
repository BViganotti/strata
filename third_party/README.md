# Vendored dependencies

Both libraries are single-header, MIT-licensed, and pinned. They are vendored so
`strata` builds offline with nothing but CMake, a C++17 compiler, git, and
SQLite.

| Library | Version | File | License |
| --- | --- | --- | --- |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.66.0 | `httplib/httplib.h` | `httplib/LICENSE` |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | `nlohmann/json.hpp` | `nlohmann/LICENSE.MIT` |

To update one, replace the header with the release's single-header artifact and
update the version in this table.
