# frozen_string_literal: false
require 'test/unit'
require 'rbconfig/sizeof'

# The interpreter stores small integers in the widest immediate form that a
# VALUE can hold, while the old C API keeps reporting C long-sized Fixnums.
# These tests exercise values that live between the two boundaries on LLP64
# platforms and coincide with the Fixnum range elsewhere.
class TestWideFixnum < Test::Unit::TestCase
  VALUE_BITS = RbConfig::SIZEOF['void*'] * 8
  WIDE_MAX = (1 << (VALUE_BITS - 2)) - 1
  WIDE_MIN = -WIDE_MAX - 1

  def test_wide_boundaries_are_integers
    assert_instance_of(Integer, WIDE_MAX)
    assert_instance_of(Integer, WIDE_MIN)
    assert_equal(WIDE_MAX + 1, WIDE_MAX.succ)
    assert_equal(WIDE_MIN - 1, WIDE_MIN.pred)
  end

  def test_wide_arithmetic
    assert_equal(WIDE_MAX, (WIDE_MAX - 1) + 1)
    assert_equal(WIDE_MIN, (WIDE_MIN + 1) - 1)
    assert_equal((WIDE_MAX / 2)**2, (WIDE_MAX / 2) * (WIDE_MAX / 2))
    assert_equal(0, WIDE_MAX + WIDE_MIN + 1)
  end

  def test_wide_to_s_round_trip
    [WIDE_MAX, WIDE_MIN, WIDE_MAX / 3, -(WIDE_MAX / 7)].each do |v|
      s = v.to_s
      assert_equal(v, s.to_i)
      assert_match(/\A-?[0-9]+\z/, s)
    end
  end

  def test_wide_marshal_round_trip
    [WIDE_MAX, WIDE_MIN].each do |v|
      assert_equal(v, Marshal.load(Marshal.dump(v)))
    end
  end

  def test_wide_hash_consistency
    h = {}
    h[WIDE_MAX] = :max
    h[WIDE_MIN] = :min
    assert_equal(:max, h[WIDE_MAX])
    assert_equal(:min, h[WIDE_MIN])
    assert_equal([WIDE_MAX].hash, [WIDE_MAX].hash)
  end

  def test_array_sum_with_wide_values
    ary = [1, WIDE_MAX - 2, 1]
    assert_equal(WIDE_MAX, ary.sum)
    assert_equal(WIDE_MAX * 2, [WIDE_MAX, WIDE_MAX].sum)
    assert_equal(WIDE_MIN, [WIDE_MIN, 0].sum(0))
  end

  def test_range_each_with_wide_bounds
    a = []
    (WIDE_MAX - 4..WIDE_MAX).each { |i| a << i }
    assert_equal([WIDE_MAX - 4, WIDE_MAX - 3, WIDE_MAX - 2, WIDE_MAX - 1, WIDE_MAX], a)

    a = []
    (WIDE_MAX - 3...WIDE_MAX).each { |i| a << i }
    assert_equal([WIDE_MAX - 3, WIDE_MAX - 2, WIDE_MAX - 1], a)

    a = []
    (WIDE_MAX..WIDE_MAX).reverse_each { |i| a << i }
    assert_equal([WIDE_MAX], a)

    seq = (WIDE_MIN + 3..WIDE_MAX).step(2)
    assert_equal([WIDE_MIN + 3, WIDE_MIN + 5], seq.take(2))
  end

  def test_case_dispatch_with_wide_literals
    v = WIDE_MAX
    matched =
      case v
      when 1 then :small
      when WIDE_MAX then :wide
      else :other
      end
    assert_equal(:wide, matched)
  end

  def test_sprintf_wide_value
    assert_equal(WIDE_MAX.to_s, sprintf('%d', WIDE_MAX))
    assert_equal(WIDE_MIN.to_s, sprintf('%d', WIDE_MIN))
  end

  def test_bit_operations_on_wide_values
    assert_equal(62, WIDE_MAX.bit_length)
    assert_equal(VALUE_BITS - 1, (1 << (VALUE_BITS - 2)).bit_length)
    assert_equal(WIDE_MAX >> 1, WIDE_MAX >> 1)
    assert_equal(0, WIDE_MAX & WIDE_MIN)
    assert_equal(-1, WIDE_MAX | -1)
    assert_equal(~WIDE_MAX, WIDE_MAX ^ -1)
    assert_equal(1, WIDE_MAX[VALUE_BITS - 3])
    assert_equal(0, WIDE_MAX[-1])
  end
end
