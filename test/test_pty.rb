# frozen_string_literal: false
require 'test/unit'
require 'shellwords'
require 'tmpdir'

begin
  require 'pty'
rescue LoadError
end

class TestPTY < Test::Unit::TestCase
  RUBY = EnvUtil.rubybin
  WINDOWS_PTY = /mswin|mingw/ =~ RUBY_PLATFORM
  # "cat" is not always available on Windows; read stdin to EOF instead.
  CAT = WINDOWS_PTY ? [RUBY, "-e", "STDIN.read"] : "cat"

  # ConPTY renders child output as a terminal screen: input-mode reports,
  # cursor hide/show, clear-screen and title updates wrap the payload.
  # Strip those control sequences so assertions compare the payload
  # like on Unix PTYs.
  def pty_gets(io)
    line = io.gets
    if WINDOWS_PTY
      line = line.gsub(/\e\][^\a]*\a/, "").gsub(/\e\[[0-9;?]*[a-zA-Z]/, "")
    end
    line
  end

  def test_spawn_without_block
    r, w, pid = PTY.spawn(RUBY, '-e', 'puts "a"; sleep 0.1')
  rescue RuntimeError
    omit $!
  else
    assert_equal("a\r\n", pty_gets(r))
  ensure
    r&.close
    w&.close
    Process.wait pid if pid
  end

  def test_spawn_with_block
    PTY.spawn(RUBY, '-e', 'puts "b"; sleep 0.1') {|r,w,pid|
      begin
        assert_equal("b\r\n", pty_gets(r))
      ensure
        r.close
        w.close
        Process.wait(pid)
      end
    }
  rescue RuntimeError
    omit $!
  end

  def test_commandline
    # Shellwords.join quotes for POSIX shells; cmd.exe mangles the
    # backslash escapes (and the child ends up waiting on stdin),
    # so build a Windows-style command line here.
    commandline = WINDOWS_PTY ?
      %Q{"#{RUBY}" -e "puts 'foo'; sleep 0.1"} :
      Shellwords.join([RUBY, '-e', 'puts "foo"; sleep 0.1'])
    PTY.spawn(commandline) {|r,w,pid|
      begin
        assert_equal("foo\r\n", pty_gets(r))
      ensure
        r.close
        w.close
        Process.wait(pid)
      end
    }
  rescue RuntimeError
    omit $!
  end

  def test_argv0
    PTY.spawn([RUBY, "argv0"], '-e', 'puts "bar"; sleep 0.1') {|r,w,pid|
      begin
        assert_equal("bar\r\n", pty_gets(r))
      ensure
        r.close
        w.close
        Process.wait(pid)
      end
    }
  rescue RuntimeError
    omit $!
  end

  def test_open_without_block
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    ret = PTY.open
  rescue RuntimeError
    omit $!
  else
    assert_kind_of(Array, ret)
    assert_equal(2, ret.length)
    assert_equal(IO, ret[0].class)
    assert_equal(File, ret[1].class)
    _, slave = ret
    assert(slave.tty?)
    assert(File.chardev?(slave.path))
  ensure
    if ret
      ret[0].close
      ret[1].close
    end
  end

  def test_open_with_block
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    r = nil
    x = Object.new
    y = PTY.open {|ret|
      r = ret;
      assert_kind_of(Array, ret)
      assert_equal(2, ret.length)
      assert_equal(IO, ret[0].class)
      assert_equal(File, ret[1].class)
      _, slave = ret
      assert(slave.tty?)
      assert(File.chardev?(slave.path))
      x
    }
  rescue RuntimeError
    omit $!
  else
    assert(r[0].closed?)
    assert(r[1].closed?)
    assert_equal(y, x)
  end

  def test_close_in_block
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    PTY.open {|master, slave|
      slave.close
      master.close
      assert(slave.closed?)
      assert(master.closed?)
    }
  rescue RuntimeError
    omit $!
  else
    assert_nothing_raised {
      PTY.open {|master, slave|
        slave.close
        master.close
      }
    }
  end

  def test_open
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    PTY.open {|master, slave|
      slave.puts "foo"
      assert_equal("foo", master.gets.chomp)
      master.puts "bar"
      assert_equal("bar", slave.gets.chomp)
    }
  rescue RuntimeError
    omit $!
  end

  def test_stat_slave
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    PTY.open {|master, slave|
      s =  File.stat(slave.path)
      assert_equal(Process.uid, s.uid)
      assert_equal(0600, s.mode & 0777)
    }
  rescue RuntimeError
    omit $!
  end

  def test_close_master
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    PTY.open {|master, slave|
      master.close
      assert_raise(EOFError) { slave.readpartial(10) }
    }
  rescue RuntimeError
    omit $!
  end

  def test_close_slave
    omit "PTY.open is not implemented on Windows" if WINDOWS_PTY
    PTY.open {|master, slave|
      slave.close
      # This exception is platform dependent.
      assert_raise(
        EOFError,       # FreeBSD
        Errno::EIO      # GNU/Linux
      ) { master.readpartial(10) }
    }
  rescue RuntimeError
    omit $!
  end

  def test_getpty_nonexistent
    bug3672 = '[ruby-dev:41965]'
    Dir.mktmpdir do |tmpdir|
      assert_raise(Errno::ENOENT, bug3672) {
        begin
          PTY.getpty(File.join(tmpdir, "no-such-command"))
        rescue RuntimeError
          omit $!
        end
      }
    end
  end

  def test_pty_check_default
    st1 = st2 = pid = nil
    `echo` # preset $?
    PTY.spawn(*CAT) do |r,w,id|
      pid = id
      st1 = PTY.check(pid)
      w.close
      r.close
      begin
        sleep(0.1)
      end until st2 = PTY.check(pid)
    end
  rescue RuntimeError
    omit $!
  else
    assert_nil(st1)
    assert_equal(pid, st2.pid)
  end

  def test_pty_check_raise
    bug2642 = '[ruby-dev:44600]'
    st1 = st2 = pid = nil
    PTY.spawn(*CAT) do |r,w,id|
      pid = id
      assert_nothing_raised(PTY::ChildExited, bug2642) {st1 = PTY.check(pid, true)}
      w.close
      r.close
      sleep(0.1)
      st2 = assert_raise(PTY::ChildExited, bug2642) {PTY.check(pid, true)}.status
    end
  rescue RuntimeError
    omit $!
  else
    assert_nil(st1)
    assert_equal(pid, st2.pid)
  end

  def test_cloexec
    unless WINDOWS_PTY # PTY.open is not implemented on Windows
      PTY.open {|m, s|
        assert(m.close_on_exec?)
        assert(s.close_on_exec?)
      }
    end
    PTY.spawn(RUBY, '-e', '') {|r, w, pid|
      begin
        assert(r.close_on_exec?)
        assert(w.close_on_exec?)
      ensure
        r.close
        w.close
        Process.wait(pid)
      end
    }
  rescue RuntimeError
    omit $!
  end
end if defined? PTY
