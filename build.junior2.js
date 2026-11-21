console.info("building foxrci ...");

const quickjs_objs = [
  "third-party/quickjs/quickjs.o",
  "third-party/quickjs/libregexp.o",
  "third-party/quickjs/libunicode.o",
  "third-party/quickjs/cutils.o",
  "third-party/quickjs/dtoa.o",
];

const cxxflags = [
  "-std=c++17",
  "-I", "third-party/quickjs",
];

// ---- 三个动态库模块 ----
// 输出到 modules/ 目录，运行时 module_loader 从这里遍历加载。
const module_srcs = [
  "modules/console/module.cpp",
  "modules/file/module.cpp",
  "modules/cmdapply/module.cpp",
];

for (const src of module_srcs) {
  const name = src.split("/")[1];                 // console / file / cmdapply
  const obj = "modules/" + name + "/module.o";
  const dll = "modules/foxrci_" + name + ".dll";
  console.info("CXX " + src);
  let r = CmdApply("g++", [...cxxflags, "-c", src, "-o", obj]);
  if (r.code !== 0) {
    console.error("compile failed: " + src + "\n" + r.stderr);
    throw "build stopped";
  }
  console.info("LD  " + dll);
  r = CmdApply("g++", ["-shared", obj, ...quickjs_objs, "-o", dll + ".new", "-lpthread"]);
  if (r.code !== 0) {
    console.error("link failed: " + dll + "\n" + r.stderr);
    throw "build stopped";
  }
  r = CmdApply("mv", ["-f", dll + ".new", dll]);
  if (r.code !== 0) {
    console.error("cannot replace " + dll + ":\n" + r.stderr);
    throw "build stopped";
  }
}

// ---- 主程序（不含内置模块，模块全部动态加载） ----
const main_srcs = [
  "foxrci.cpp",
  "commands/init.cpp",
  "parser/parser.cpp",
  "parser/module_loader.cpp",
];

const main_objs = [];
for (const src of main_srcs) {
  const obj = src.replace(/\.cpp$/, ".o");
  console.info("CXX " + src);
  const r = CmdApply("g++", [...cxxflags, "-c", src, "-o", obj]);
  if (r.code !== 0) {
    console.error("compile failed: " + src + "\n" + r.stderr);
    throw "build stopped";
  }
  main_objs.push(obj);
}

console.info("LD  foxrci.exe");
const ld_r = CmdApply("g++", [
  ...main_objs,
  ...quickjs_objs,
  "-o", "foxrci.exe.new",
  "-lpthread",
]);
if (ld_r.code !== 0) {
  console.error("link failed:\n" + ld_r.stderr);
  throw "build stopped";
}
const mv_r = CmdApply("mv", ["-f", "foxrci.exe.new", "foxrci.exe"]);
if (mv_r.code !== 0) {
  console.error("cannot replace foxrci.exe:\n" + mv_r.stderr);
  throw "build stopped";
}

const log = new File("build.log");
if (!log.write("build ok: foxrci.exe")) {
  console.error("cannot write build.log");
}

console.log("done. output: foxrci.exe");
