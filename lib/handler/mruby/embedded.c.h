/*
 * DO NOT EDIT! generated by embed_mruby_code.pl
 * Please refer to the respective source files for copyright information.
 */

/* lib/handler/mruby/embedded/core.rb */
#define H2O_MRUBY_CODE_CORE                                                                                                        \
    "$__TOP_SELF__ = self\n"                                                                                                       \
    "def _h2o_eval_conf(__h2o_conf)\n"                                                                                             \
    "  $__TOP_SELF__.eval(__h2o_conf[:code], nil, __h2o_conf[:file], __h2o_conf[:line])\n"                                         \
    "end\n"                                                                                                                        \
    "module Kernel\n"                                                                                                              \
    "  def task(&block)\n"                                                                                                         \
    "    fiber = Fiber.new do\n"                                                                                                   \
    "      block.call\n"                                                                                                           \
    "      # For when it's called in h2o_mruby_run_fiber and return output,\n"                                                     \
    "      # or block doesn't have asynchronous callback\n"                                                                        \
    "      [0, nil, nil]\n"                                                                                                        \
    "    end\n"                                                                                                                    \
    "    _h2o__run_child_fiber(proc { fiber.resume })\n"                                                                           \
    "  end\n"                                                                                                                      \
    "  def _h2o_define_callback(name, callback_id)\n"                                                                              \
    "    Kernel.define_method(name) do |*args|\n"                                                                                  \
    "      ret = Fiber.yield([ callback_id, _h2o_create_resumer(), args ])\n"                                                      \
    "      if ret.kind_of? Exception\n"                                                                                            \
    "        raise ret\n"                                                                                                          \
    "      end\n"                                                                                                                  \
    "      ret\n"                                                                                                                  \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "  def _h2o_create_resumer()\n"                                                                                                \
    "    me = Fiber.current\n"                                                                                                     \
    "    Proc.new do |v|\n"                                                                                                        \
    "      me.resume(v)\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "  def _h2o_proc_each_to_array()\n"                                                                                            \
    "    Proc.new do |o|\n"                                                                                                        \
    "      a = []\n"                                                                                                               \
    "      o.each do |x|\n"                                                                                                        \
    "        a << x\n"                                                                                                             \
    "      end\n"                                                                                                                  \
    "      a\n"                                                                                                                    \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "  def _h2o_prepare_app(conf)\n"                                                                                               \
    "    app = Proc.new do |req|\n"                                                                                                \
    "      _h2o__block_request(req)\n"                                                                                             \
    "    end\n"                                                                                                                    \
    "    cached = nil\n"                                                                                                           \
    "    runner = Proc.new do |args|\n"                                                                                            \
    "      fiber = cached || Fiber.new do |req, generator|\n"                                                                      \
    "        self_fiber = Fiber.current\n"                                                                                         \
    "        while 1\n"                                                                                                            \
    "          begin\n"                                                                                                            \
    "            while 1\n"                                                                                                        \
    "              resp = app.call(req)\n"                                                                                         \
    "              cached = self_fiber\n"                                                                                          \
    "              (req, generator) = Fiber.yield(*resp, generator)\n"                                                             \
    "            end\n"                                                                                                            \
    "          rescue => e\n"                                                                                                      \
    "            cached = self_fiber\n"                                                                                            \
    "            (req, generator) = _h2o__send_error(e, generator)\n"                                                              \
    "          end\n"                                                                                                              \
    "        end\n"                                                                                                                \
    "      end\n"                                                                                                                  \
    "      cached = nil\n"                                                                                                         \
    "      fiber.resume(*args)\n"                                                                                                  \
    "    end\n"                                                                                                                    \
    "    configurator = Proc.new do\n"                                                                                             \
    "      fiber = Fiber.new do\n"                                                                                                 \
    "        begin\n"                                                                                                              \
    "          H2O::ConfigurationContext.reset\n"                                                                                  \
    "          app = _h2o_eval_conf(conf)\n"                                                                                       \
    "          H2O::ConfigurationContext.instance.call_post_handler_generation_hooks(app)\n"                                       \
    "          _h2o__run_blocking_requests()\n"                                                                                    \
    "        rescue => e\n"                                                                                                        \
    "          app = Proc.new do |req|\n"                                                                                          \
    "            [500, {}, ['Internal Server Error']]\n"                                                                           \
    "          end\n"                                                                                                              \
    "          _h2o__run_blocking_requests(e)\n"                                                                                   \
    "        end\n"                                                                                                                \
    "      end\n"                                                                                                                  \
    "      fiber.resume\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "    [runner, configurator]\n"                                                                                                 \
    "  end\n"                                                                                                                      \
    "  def sleep(*sec)\n"                                                                                                          \
    "    _h2o__sleep(*sec)\n"                                                                                                      \
    "  end\n"                                                                                                                      \
    "end\n"

/* lib/handler/mruby/embedded/http_request.rb */
#define H2O_MRUBY_CODE_HTTP_REQUEST                                                                                                \
    "module H2O\n"                                                                                                                 \
    "  class HttpRequest\n"                                                                                                        \
    "    def join\n"                                                                                                               \
    "      if !@resp\n"                                                                                                            \
    "        @resp = _h2o__http_join_response(self)\n"                                                                             \
    "      end\n"                                                                                                                  \
    "      @resp\n"                                                                                                                \
    "    end\n"                                                                                                                    \
    "    def _set_response(resp)\n"                                                                                                \
    "      @resp = resp\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "  class HttpInputStream\n"                                                                                                    \
    "    def each\n"                                                                                                               \
    "      first = true\n"                                                                                                         \
    "      while c = _h2o__http_fetch_chunk(self, first)\n"                                                                        \
    "        yield c\n"                                                                                                            \
    "        first = false\n"                                                                                                      \
    "      end\n"                                                                                                                  \
    "    end\n"                                                                                                                    \
    "    def join\n"                                                                                                               \
    "      s = \"\"\n"                                                                                                             \
    "      each do |c|\n"                                                                                                          \
    "        s << c\n"                                                                                                             \
    "      end\n"                                                                                                                  \
    "      s\n"                                                                                                                    \
    "    end\n"                                                                                                                    \
    "    class Empty < HttpInputStream\n"                                                                                          \
    "      def each; end\n"                                                                                                        \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "end\n"

/* lib/handler/mruby/embedded/chunked.rb */
#define H2O_MRUBY_CODE_CHUNKED                                                                                                     \
    "module Kernel\n"                                                                                                              \
    "  def _h2o_chunked_proc_each_to_fiber()\n"                                                                                    \
    "    Proc.new do |args|\n"                                                                                                     \
    "      src, generator = *args\n"                                                                                               \
    "      fiber = Fiber.new do\n"                                                                                                 \
    "        begin\n"                                                                                                              \
    "          src.each do |chunk|\n"                                                                                              \
    "            _h2o_send_chunk(chunk, generator)\n"                                                                              \
    "          end\n"                                                                                                              \
    "          _h2o_send_chunk_eos(generator)\n"                                                                                   \
    "        rescue => e\n"                                                                                                        \
    "          _h2o__send_error(e, generator)\n"                                                                                   \
    "        end\n"                                                                                                                \
    "      end\n"                                                                                                                  \
    "      fiber.resume\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "end\n"

/* lib/handler/mruby/embedded/channel.rb */
#define H2O_MRUBY_CODE_CHANNEL                                                                                                     \
    "module H2O\n"                                                                                                                 \
    "  class Channel\n"                                                                                                            \
    "    def push(o)\n"                                                                                                            \
    "      @queue << o\n"                                                                                                          \
    "      self._notify\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "    def shift\n"                                                                                                              \
    "      if @queue.empty?\n"                                                                                                     \
    "        _h2o__channel_wait(self)\n"                                                                                           \
    "      end\n"                                                                                                                  \
    "      @queue.shift\n"                                                                                                         \
    "    end\n"                                                                                                                    \
    "  end\n"                                                                                                                      \
    "end\n"
