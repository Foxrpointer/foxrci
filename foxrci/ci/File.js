class File
{
  filepath = ''
  content = ''
  constructor(filepath) {
    this.filepath = filepath
  }

  file_exists() {
    // content==null && if_exists_stop==true -> check file exists
    return !(WriteFile(this.filepath, "", true))
  }

  del()
  {
    return (WriteFile(this.filepath, '', false))
  }

  // clear the (all of the content), before write
  // important! don't input a empty content
  write(content)
  {
    return (WriteFile(this.filepath, content, false))
  }

  // write depend on (exists content in old file)
  // important! don't input a empty content
  commit()
  {
    return (WriteFile(this.filepath, this.content, false))
  }

  // write content before (writing file)
  write_content(content) {
    this.content = content
  }

  // write a line of text to content, before (writing file)
  // it will type '\n' autoly
  write_line(text)
  {
    this.content += text + '\n'
  }
}

export default File;
