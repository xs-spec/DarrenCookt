? my $ctx = $main::context;
? $_mt->wrapper_file("wrapper.mt", "Configure", "Status Directives")->(sub {

<p>
The status handler exposes the current states of the HTTP server.
This document describes the configuration directives of the handler.
</p>

<?
$ctx->{directive}->(
    name    => "status",
    levels  => [ qw(path) ],
    since   => '2.0',
    desc    => <<'EOT',
If the argument is <code>ON</code>, the directive registers the status handler to the current path.
EOT
)->(sub {
?>
<p>
Access to the handler should be <a href="configure/mruby.html#access-control">restricted</a>, considering the fact that the status includes the details of in-flight HTTP requests.
The example below uses <a href="configure/basic_auth.html">Basic authentication</a>.
</p>
<?= $ctx->{example}->("Exposing status with Basic authentication", <<'EOT');
paths:
  /server-status:
    mruby.handler: |
      require "htpasswd.rb"
      Htpasswd.new("/path/to/.htpasswd", "status")
    status: ON
EOT
?>
? })

? })
