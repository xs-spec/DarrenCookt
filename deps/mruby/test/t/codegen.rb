##
# Codegen tests

assert('peephole optimization does not eliminate move whose result is reused') do
  assert_raise LocalJumpError do
    def method
      yield
    end
    method(&a &&= 0)
  end
end

assert('empty condition in ternary expression parses correctly') do
  assert_equal(() ? 1 : 2, 2)
end

assert('codegen absorbs arguments to redo and retry if they are the argument of a call') do
  assert_nothing_raised do
    a=*"1", case nil
    when 1
      redo |
      1
    end
  end

  assert_nothing_raised do
    a=*"1", case nil
    when 1
      retry |
      1
    end
  end
end

assert('method call with exactly 127 arguments') do
  def args_to_ary(*args)
    args
  end

  assert_equal [0]*127, args_to_ary(
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, \
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
  )
end

assert('nested empty heredoc') do
  _, a = nil, <<B
#{<<A}
A
B
  assert_equal "\n", a
end

assert('splat in case splat') do
  a = *case
    when 0
      * = 1
  end

  assert_equal [1], a
end
