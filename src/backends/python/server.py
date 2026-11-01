#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# External server for Cantor's Python backend.
# Wire protocol:
#   message      := command RECORD_SEP arguments MESSAGE_END
#   code result  := output UNIT_SEP error UNIT_SEP isError MESSAGE_END
#   model result := records UNIT_SEP MESSAGE_END
# Separators: RECORD_SEP=chr(30), UNIT_SEP=chr(31), MESSAGE_END=chr(29),
#             FIELD_SEP=chr(17), VAR_SEP=chr(18).

import sys


def _start_server():
    # The whole server implementation lives in the local scope of this
    # function, so that no internal name leaks into the user namespace.
    import os
    import traceback
    import types

    _sys = sys

    RECORD_SEP = chr(30)
    UNIT_SEP = chr(31)
    MESSAGE_END = chr(29)
    FIELD_SEP = chr(17)
    VAR_SEP = chr(18)
    MESSAGE_END_BYTES = MESSAGE_END.encode('utf-8')

    stdout_fd = _sys.stdout.fileno()
    stdin_fd = _sys.stdin.fileno()
    file_path = 'python_cantor_worksheet'

    # User code is executed in the namespace of the __main__ module, like in
    # the embedded server. This keeps introspection working: `from __main__
    # import __dict__` (used by the code completion) and `globals()` must see
    # the user's variables.
    user_globals = _sys.modules['__main__'].__dict__
    user_globals.pop('_start_server', None)


    class CatchOut:
        def __init__(self, std_stream):
            self.value = ''
            self.encoding = getattr(std_stream, 'encoding', None) or 'utf-8'

        def flush(self):
            pass

        def write(self, text):
            self.value += text

        def writelines(self, lines):
            self.value += ''.join(lines)

        def isatty(self):
            return False


    output_backend = CatchOut(_sys.stdout)
    error_backend = CatchOut(_sys.stderr)

    # The embedded server exposes these names to the user's code as well,
    # keep them for compatibility with existing worksheets.
    user_globals['CatchOutPythonBackend'] = CatchOut
    user_globals['outputPythonBackend'] = output_backend
    user_globals['errorPythonBackend'] = error_backend
    user_globals['sys'] = _sys


    def send(text):
        data = text.encode('utf-8', 'replace')
        while data:
            written = os.write(stdout_fd, data)
            data = data[written:]


    def print_user_traceback(stream, exc_type, exc_value, exc_tb):
        # Print only the frames of the user's code, the server's own frames
        # (compile/exec and this handler) are not relevant for the user.
        frames = []
        tb = exc_tb
        while tb is not None:
            code = tb.tb_frame.f_code
            if code.co_filename == file_path:
                # Build the summary without the frame globals: the user code
                # runs in the __main__ namespace, whose loader would resolve
                # any file name to the server script itself.
                frames.append(traceback.FrameSummary(code.co_filename, tb.tb_lineno, code.co_name))
            tb = tb.tb_next
        lines = []
        if frames:
            lines.append('Traceback (most recent call last):\n')
            lines.extend(traceback.format_list(frames))
        lines.extend(traceback.format_exception_only(exc_type, exc_value))
        stream.write(''.join(lines))


    def run_code(command):
        out = output_backend
        err = error_backend
        out.value = ''
        err.value = ''
        old_out = _sys.stdout
        old_err = _sys.stderr
        _sys.stdout = out
        _sys.stderr = err
        is_error = False
        try:
            try:
                code = compile(command, file_path, 'single')
            except (SyntaxError, ValueError):
                code = compile(command, file_path, 'exec')
            exec(code, user_globals, user_globals)
        except KeyboardInterrupt:
            # Like the embedded server, do not answer a command that was
            # interrupted; a late answer could be matched to the next
            # expression by the client.
            return None
        except BaseException:
            print_user_traceback(err, *_sys.exc_info())
            is_error = True
        finally:
            _sys.stdout = old_out
            _sys.stderr = old_err

        return out.value + UNIT_SEP + err.value + UNIT_SEP + ('1' if is_error else '0') + MESSAGE_END


    def set_file_path(path, directory):
        nonlocal file_path
        _sys.argv = [path]
        if not path:
            _sys.path.insert(0, '')
        else:
            file_path = path
            _sys.path.insert(0, directory)
            user_globals['__file__'] = path


    def get_variables(parse_value):
        numpy_module = None
        numpy_threshold = None
        try:
            import numpy
            numpy_module = numpy
            numpy_threshold = numpy.get_printoptions()['threshold']
            numpy.set_printoptions(threshold=100000000)
        except Exception:
            numpy_module = None
            numpy_threshold = None

        records = []
        for name, value in list(user_globals.items()):
            if name.startswith('__'):
                continue
            if name in ('CatchOut', 'CatchOutPythonBackend', 'errorPythonBackend', 'outputPythonBackend'):
                continue
            if isinstance(value, (types.ModuleType, types.FunctionType, type)):
                continue

            value_string = ''
            size_string = ''
            type_string = ''
            if parse_value:
                try:
                    value_string = repr(value)
                except BaseException:
                    value_string = ''
                try:
                    size_string = repr(_sys.getsizeof(value))
                except BaseException:
                    size_string = ''
                try:
                    type_string = repr(type(value))
                except BaseException:
                    type_string = ''

            records.append(name + FIELD_SEP + value_string + FIELD_SEP + size_string + FIELD_SEP + type_string)

        if numpy_module is not None:
            try:
                numpy_module.set_printoptions(threshold=numpy_threshold)
            except BaseException:
                pass

        return ''.join(record + VAR_SEP for record in records) + VAR_SEP


    def handle(message):
        records = message.split(RECORD_SEP)
        if len(records) != 2:
            return None

        command = records[0]
        arguments = records[1]

        if command == 'exit':
            return False

        if command == 'login':
            return None

        if command == 'setFilePath':
            args = arguments.split(UNIT_SEP)
            if len(args) == 2:
                set_file_path(args[0], args[1])
            return None

        if command == 'code':
            return run_code(arguments)

        if command == 'model':
            try:
                parse_value = bool(int(arguments))
            except ValueError:
                return UNIT_SEP + "Invalid argument for 'model' command" + MESSAGE_END
            try:
                return get_variables(parse_value) + UNIT_SEP + MESSAGE_END
            except BaseException:
                # Always answer the model command, otherwise the client's
                # expression would wait forever for the reply.
                traceback.print_exc(file=_sys.stderr)
                return UNIT_SEP + MESSAGE_END

        return None


    def main():
        send('ready')
        buffer = b''
        while True:
            try:
                chunk = os.read(stdin_fd, 4096)
            except KeyboardInterrupt:
                continue
            except OSError:
                break

            if not chunk:
                break

            buffer += chunk
            while MESSAGE_END_BYTES in buffer:
                raw, buffer = buffer.split(MESSAGE_END_BYTES, 1)
                message = raw.decode('utf-8', 'replace')
                try:
                    result = handle(message)
                except KeyboardInterrupt:
                    continue
                except BaseException:
                    traceback.print_exc(file=_sys.stderr)
                    result = None

                if result is False:
                    return
                if result:
                    send(result)

    main()


if __name__ == '__main__':
    _start_server()
