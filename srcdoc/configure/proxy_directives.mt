? my $ctx = $main::context;
? $_mt->wrapper_file("wrapper.mt", "Configure", "Proxy Directives")->(sub {

<p>
This document describes the configuration directives of the proxy handler.
</p>

<?
$ctx->{directive}->(
    name    => "proxy.reverse.url",
    levels  => [ qw(path) ],
    desc    => q{Forwards the requests to the specified URL, and proxies the response.},
)->(sub {
?>
<?= $ctx->{example}->(q{Forwarding the requests to application server running on <code>127.0.0.1:8080</code>}, <<'EOT')
proxy.reverse.url: "http://127.0.0.1:8080/"
EOT
?>
<p>
At the moment, only HTTP is supported.
If you want load balancing multiple backends, replace 127.0.0.1 with hostname witch returns IP addresses via DNS or /etc/hosts.
</p>
<p>
In addition to TCP/IP over IPv4 and IPv6, the proxy handler can also connect to an HTTP server listening to a Unix socket.
Path to the unix socket should be surrounded by square brackets, and prefixed with <code>unix:</code> (e.g. <code>http://[unix:/path/to/socket]/path</code>).
</p>

? })

<?
$ctx->{directive}->(
    name    => "proxy.preserve-host",
    levels  => [ qw(global host path) ],
    default => q{proxy.preserve-host: OFF},
    desc    => q{A boolean flag (<code>ON</code> or <code>OFF</code>) designating whether or not to pass <code>Host</code> header from incoming request to upstream.},
)->(sub {});

$ctx->{directive}->(
    name    => "proxy.timeout.io",
    levels  => [ qw(global host path) ],
    default => q{proxy.timeout.io: 30000},
    desc    => q{Sets the upstream I/O timeout in milliseconds.},
)->(sub {});
?>

<?
$ctx->{directive}->(
    name    => "proxy.timeout.keepalive",
    levels  => [ qw(global host path) ],
    default => q{proxy.timeout.keepalive: 2000},
    desc    => 'Sets the upstream timeout for idle connections in milliseconds.',
)->(sub {
?>
<p>
Upstream connection becomes non-persistent if the value is set to zero.
The value should be set to something smaller than that being set at the upstream server.
</p>
? })

<?
$ctx->{directive}->(
    name    => "proxy.websocket",
    levels  => [ qw(global host path) ],
    default => q{proxy.websocket: OFF},
    desc    => q{A boolean flag (<code>ON</code> or <code>OFF</code>) indicating whether or not to allow upgrading the proxied connection to <a href="https://tools.ietf.org/html/rfc6455">the WebSocket protocol</a>.},
)->(sub {
?>
<p>
When set to <code>ON</code>, the proxied connection will be upgraded to a bi-directional tunnel stream if upgrading to WebSocket connection is permitted by the backend server (i.e. if the backend server responds to a WebSocket handshake with <code>101</code> status code).
</p>
<p>
Support for WebSocket is considered experimental for the time being and therefore is not yet turned on by default.
</p>
? })

<?
$ctx->{directive}->(
    name    => "proxy.websocket.timeout",
    levels  => [ qw(global host path) ],
    default => q{proxy.websocket.timeout: 300000},
    desc    => q{Sets idle timeout of a WebSocket connection being proxied.},
)->(sub {})
?>

? })
