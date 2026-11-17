class Main {
    constructor(ci) {
        let cp = ci.create_project();

        cp.name("FoxrCI")
          .type("exe")
          .sources(["app.cpp"])
          .include(["lib/quickjs"])
          .define(["WIN32"])
          .cxxFlags(["-std=gnu++17"])
          .linkFlags(["-Wl,-e,mainCRTStartup"])
          .link(["gdi32", "user32"])
          .commit()
          .commit();
    }
}
