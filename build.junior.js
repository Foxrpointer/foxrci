import console from "foxrci/console.js"

console.info("building foxrci ...");

// 编译 QuickJS 静态库
const quickjs_objs = [
  "third-party/quickjs/quickjs.c",
  "third-party/quickjs/libregexp.c",
  "third-party/quickjs/libunicode.c",
  "third-party/quickjs/cutils.c",
  "third-party/quickjs/dtoa.c",
];

const quickjs_cflags = [
  "-I", "third-party/quickjs",
  "-DCONFIG_VERSION=\"2026-06-04\"",
  "-c",
];

// 先编译各 .c 为 .o
for (const src of quickjs_objs) {
  const obj = src.replace(/\.c$/, ".o");
  console.info("CC  " + src);
  const r = CmdApply("gcc", [...quickjs_cflags, src, "-o", obj]);
  if (r.code !== 0) {
    console.error("compile failed: " + src + "\n" + r.stderr);
    throw "build stopped";
  }
}

// 打包静态库
console.info("AR  libquickjs.a");
const ar_r = CmdApply("ar", [
  "rcs", "libquickjs.a",
  ...quickjs_objs.map(s => s.replace(/\.c$/, ".o")),
]);
if (ar_r.code !== 0) {
  console.error("ar failed:\n" + ar_r.stderr);
  throw "build stopped";
}

// 编译主程序源文件
const main_srcs = [
  "foxrci.cpp",
  "commands/init.cpp",
  "commands/run_js.cpp",
  "js-libc/console.cpp",
  "js-libc/writefile.cpp",
  "js-libc/cmdapply.cpp",
];

const main_cxxflags = [
  "-std=c++17",
  "-I", "third-party/quickjs",
  "-I", "commands",
  "-c",
];

const main_objs = [];
for (const src of main_srcs) {
  const obj = src.replace(/\.cpp$/, ".o");
  console.info("CXX " + src);
  const r = CmdApply("g++", [...main_cxxflags, src, "-o", obj]);
  if (r.code !== 0) {
    console.error("compile failed: " + src + "\n" + r.stderr);
    throw "build stopped";
  }
  main_objs.push(obj);
}

// 链接
console.info("LD  foxrci.exe");
const ld_r = CmdApply("g++", [
  ...main_objs,
  "libquickjs.a",
  "-o", "foxrci.exe",
  "-lpthread",
]);
if (ld_r.code !== 0) {
  console.error("link failed:\n" + ld_r.stderr);
  throw "build stopped";
}

// 写构建日志
if (!WriteFile("build.log", "build ok: foxrci.exe")) {
  console.error("cannot write build.log");
}

console.log("done. output: foxrci.exe");
