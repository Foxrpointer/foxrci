import console from "./foxrci/console.js";
import File from "./foxrci/ci/File.js";

console.info("test by kyuujuu")
let f = new File("a_txt")

// exists
let file_exists = f.file_exists()
console.warn("file exists: " + file_exists)

// create
f.write("content")
console.warn("created a_txt")

// write lines
f.write_line("text")
f.write_line("text2")
f.commit()

// del
f.del()
console.warn("del a_txt")
