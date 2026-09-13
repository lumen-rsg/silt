"""One bounded request refusal through QEMU GDB; no lifecycle-state writes."""

from pathlib import Path
import subprocess
import time


class PipelineFault:
    def __init__(self, kernel, port, pid, phase, ordinal, log):
        if phase not in ('handoff', 'resume') or not 1 <= ordinal <= 16 or pid <= 0:
            raise ValueError('invalid pipeline fault target')
        self.log = Path(log)
        self.stream = self.log.open('w')
        script = f'''
import gdb
class Refusal(gdb.Breakpoint):
    count = 0
    prefix = []
    def stop(self):
        if {phase!r} == 'resume':
            request = gdb.parse_and_eval('(NevaMessage*)$x1')
            caller = request['caller']
            if not int(caller) or int(caller['id']) != {pid} or int(request['method']) != 5:
                return False
            self.count += 1
            process = gdb.parse_and_eval('(NevaProcessObject*)$x0')
            print('RESUME_REQUEST ordinal=%d child=%d state=%d' %
                  (self.count, int(process['process_id']), int(process['state'])), flush=True)
            if int(process['state']) != int(gdb.parse_and_eval('NEVA_PROCESS_SUSPENDED')):
                raise RuntimeError('target is not suspended')
            if self.count != {ordinal}:
                self.prefix.append(int(process))
                return False
            for address in self.prefix:
                previous = gdb.parse_and_eval('(NevaProcessObject*)%d' % address)
                if not int(previous['resume_consumed']):
                    raise RuntimeError('prefix resume did not succeed')
                print('PREFIX_RESUMED child=%d' % int(previous['process_id']), flush=True)
            # Change only this dispatched request, before the handler reads it.
            gdb.execute('set ((NevaMessage*)$x1)->method = 65535')
        else:
            request = gdb.parse_and_eval('(TrapFrame*)$x0')
            caller = gdb.parse_and_eval('(NevaThread*)$x1')
            if int(caller['id']) != {pid} or int(request['x'][1]) != 25:
                return False
            self.count += 1
            if self.count != {ordinal}:
                return False
            generation = int(request['x'][2])
            if not 0 < generation < 0xffffffff:
                raise RuntimeError('unexpected handoff generation')
            gdb.execute('set ((TrapFrame*)$x0)->x[2] = %d' % (generation + 1))
        print('FAULT_INJECTED phase={phase} ordinal={ordinal} pid={pid}', flush=True)
        return True
Refusal('*process_rpc' if {phase!r} == 'resume' else '*service_syscall_call', internal=True)
print('FAULT_ARMED', flush=True)
gdb.execute('continue')
gdb.execute('delete breakpoints')
gdb.execute('detach')
'''
        self.proc = subprocess.Popen(
            ['gdb', '-q', '-nx', '-batch', str(kernel),
             '-ex', 'set pagination off', '-ex', 'set confirm off',
             '-ex', 'set may-call-functions off', '-ex', 'set remotetimeout 3',
             '-ex', f'target remote 127.0.0.1:{port}',
             '-ex', 'python exec(' + repr(script) + ')'],
            stdout=self.stream, stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 8
            while 'FAULT_ARMED' not in self.log.read_text():
                if self.proc.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(self.log.read_text())
                time.sleep(.025)
        except BaseException:
            self.close()
            raise

    def finish(self):
        self.proc.wait(timeout=8)
        output = self.log.read_text()
        if self.proc.returncode or 'FAULT_INJECTED' not in output or 'Traceback' in output:
            raise RuntimeError(output)
        return output

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.stream.close()
