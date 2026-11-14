"""Apply LTO at link time as well as compile time to keep the OTA image small."""

Import("env")
env.Append(LINKFLAGS=["-flto"])
