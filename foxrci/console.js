const console = {
  log: (...a) => console_log(...a),
  info: (...a) => console_info(...a),
  warn: (...a) => console_warn(...a),
  error: (...a) => console_error(...a),
}

export default console
