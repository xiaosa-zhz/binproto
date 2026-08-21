add_rules("mode.debug", "mode.release")
set_languages("c++26")
set_encodings("utf-8")
add_rules("plugin.compile_commands.autoupdate")

target("binproto")
    set_kind("binary")
    add_cxflags("-freflection", {force = true})
    add_files("src/*.cc")
    add_includedirs("include")

