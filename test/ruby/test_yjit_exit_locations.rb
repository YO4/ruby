# frozen_string_literal: true
#
# This set of tests can be run with:
# make test-all TESTS='test/ruby/test_yjit_exit_locations.rb'

require 'test/unit'
require 'envutil'
require 'tmpdir'
require_relative '../lib/jit_support'
if RUBY_PLATFORM =~ /mingw|mswin/
  require 'socket'
end

return unless JITSupport.yjit_supported?

# Tests for YJIT with assertions on tracing exits
# insipired by the RJIT tests in test/ruby/test_yjit.rb
class TestYJITExitLocations < Test::Unit::TestCase
  def test_yjit_trace_exits_and_v_no_error
    _stdout, stderr, _status = EnvUtil.invoke_ruby(%w(-v --yjit-trace-exits), '', true, true)
    refute_includes(stderr, "NoMethodError")
  end

  def test_trace_exits_expandarray_splat
    assert_exit_locations('*arr = []')
  end

  private

  def assert_exit_locations(test_script)
    write_results = <<~RUBY
      IO.open(3, "wb").write Marshal.dump({
        enabled: RubyVM::YJIT.trace_exit_locations_enabled?,
        exit_locations: RubyVM::YJIT.exit_locations
      })
    RUBY

    script = <<~RUBY
      _test_proc = -> {
        #{test_script}
      }
      result = _test_proc.call
      #{write_results}
    RUBY

    run_script = eval_with_jit(script)
    # If stats are disabled when configuring, --yjit-exit-locations
    # can't be true. We don't want to check if exit_locations hash
    # is not empty because that could indicate a bug in the exit
    # locations collection.
    return unless run_script[:enabled]
    exit_locations = run_script[:exit_locations]

    assert exit_locations.key?(:raw)
    assert exit_locations.key?(:frames)
    assert exit_locations.key?(:lines)
    assert exit_locations.key?(:samples)
    assert exit_locations.key?(:missed_samples)
    assert exit_locations.key?(:gc_samples)

    assert_equal 0, exit_locations[:missed_samples]
    assert_equal 0, exit_locations[:gc_samples]

    assert_not_empty exit_locations[:raw]
    assert_not_empty exit_locations[:frames]
    assert_not_empty exit_locations[:lines]

    exit_locations[:frames].each do |frame_id, frame|
      assert frame.key?(:name)
      assert frame.key?(:file)
      assert frame.key?(:samples)
      assert frame.key?(:total_samples)
      assert frame.key?(:edges)
    end
  end

  def eval_with_jit(script)
    if RUBY_PLATFORM =~ /mingw|mswin/
      sockname = File.join(ENV["USERPROFILE"].gsub("\\","/"), "#{method_name}@#{Process.pid}.sock") # Use path shorter than tmpdir
    end
    args = [
      "--disable-gems",
      "--yjit-call-threshold=1",
      "--yjit-trace-exits"
    ]
    if sockname
      args << "-rsocket"
      args << "-e"
      args << script_shell_encode("Object.class_variable_set(:@@stat_w, UNIXSocket.new(\"#{sockname}\"))")
    end
    args << "-e" << script_shell_encode(script)
    if sockname
      srv = UNIXServer.new(sockname)
    else
      stats_r, stats_w = IO.pipe
    end
    ios = sockname ? {} : { 3 => stats_w }
    _out, _err, _status = EnvUtil.invoke_ruby(args,
                                              '', true, true, timeout: 1000, ios: ios
                                             )
    if sockname
      result = IO.select([srv], [], [], timeout = 1000 * 2)
      if result
        stats_r = srv.accept
      else
        raise Timeout::Error # unreachable
      end
    else
      stats_w.close
    end
    stats = stats_r.read
    stats = Marshal.load(stats) if !stats.empty?
    stats_r.close
    stats
  ensure
    stats_r&.close
    stats_w&.close
    if sockname
      srv&.close
      begin
        File.unlink sockname
      rescue StandardError
      end
    end
  end

  def script_shell_encode(s)
    # We can't pass utf-8-encoded characters directly in a shell arg. But we can use Ruby \u constants.
    s.chars.map { |c| c.ascii_only? ? c : "\\u%x" % c.codepoints[0] }.join
  end
end
