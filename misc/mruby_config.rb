MRuby::Build.new do |conf|
  # load specific toolchain settings

  # Gets set by the VS command prompts.
  if ENV['MRUBY_TOOLCHAIN']
    toolchain ENV['MRUBY_TOOLCHAIN']
  elsif ENV['VisualStudioVersion'] || ENV['VSINSTALLDIR']
    toolchain :visualcpp
  else
    toolchain :gcc
  end

  enable_debug

  # Use mrbgems
  conf.gem '../deps/mruby-onig-regexp'

  # include all the core GEMs
  conf.gembox 'full-core'
end
