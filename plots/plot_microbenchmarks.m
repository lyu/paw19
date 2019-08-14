clear;clc;


%% putmem/getmem
s = [1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576];
sl = {'1', '', '4', '', '16', '', '64', '', '256', '', '1K', '', '4K', '', '16K', '', '64K', '', '256K', '', '1M'};

put_mr = [
0.217 0.213 0.213 0.208 0.200 0.223 0.226 0.237 0.968 0.470 0.533 0.627  0.850  1.276  5.655  8.143  13.395  23.651  43.506  83.654  164.208;
0.652 0.273 0.265 0.254 0.251 0.276 0.269 0.490 1.686 1.271 0.546 0.651  1.533  2.375 11.504 14.482  22.991  23.682  66.468  84.059  170.570;
1.170 1.135 1.409 1.274 1.111 1.084 1.034 0.923 2.317 2.471 2.672 2.937  4.053  5.188 16.000 22.612  39.741  80.633 161.086 322.175  644.104;
1.678 1.800 1.741 1.515 1.432 1.282 1.441 1.624 2.831 2.769 2.969 3.697  4.790  6.273 20.675 28.993  59.512 120.039 241.127 482.186  964.458;
3.369 2.347 2.036 2.010 1.888 2.367 2.450 2.532 4.899 4.666 4.853 5.725  7.059 10.221 21.962 39.387  79.696 160.556 321.592 643.593 1287.613;
4.104 2.841 2.525 2.376 2.577 3.129 3.142 3.361 6.042 5.713 6.211 5.064  5.853 15.552 27.477 49.994  99.505 200.373 401.411 803.621 1607.725;
4.979 3.465 2.769 2.494 2.878 3.429 3.111 3.765 8.109 7.400 6.882 7.823 12.753 13.054 33.835 59.529 119.913 240.610 482.888 967.410 1935.609;
];

put_mr = 1000000 ./ put_mr;

put_mr_ctx = [
0.131 0.131 0.131 0.132 0.130 0.146 0.146 0.144 0.693 0.259 0.313 0.419 0.651  1.289  5.641  8.111  13.376  23.406  43.756  83.644  164.222;
0.160 0.158 0.158 0.158 0.157 0.174 0.174 0.176 1.153 0.358 0.409 0.653 1.299  2.612  5.783 10.047  20.085  40.170  80.274 160.479  321.054;
0.160 0.158 0.158 0.158 0.157 0.174 0.173 0.179 1.594 0.369 0.722 1.363 2.678  5.367 10.044 20.079  40.137  80.266 160.541 321.011  642.234;
0.149 0.148 0.146 0.147 0.147 0.162 0.160 0.222 1.922 0.595 1.095 2.072 4.063  8.114 15.069 30.109  60.211 120.268 240.778 481.678  963.401;
0.160 0.156 0.156 0.156 0.155 0.198 0.199 0.297 2.417 0.804 1.464 2.769 5.424 10.813 20.085 40.153  80.279 160.498 320.980 642.128 1284.541;
0.162 0.162 0.163 0.162 0.164 0.246 0.248 0.372 2.887 1.006 1.830 3.452 6.774 13.511 25.106 50.192 100.330 200.561 401.173 802.714 1605.436;
0.196 0.196 0.196 0.196 0.195 0.295 0.295 0.445 3.251 1.271 2.210 4.153 8.148 16.270 30.097 60.174 120.438 240.750 481.480 963.077 1926.742;
];

put_mr_ctx = 1000000 ./put_mr_ctx;

put_mr_pure = [
0.221 0.220 0.212 0.213 0.213 0.239 0.240 0.239 0.976 0.476 0.532 0.640 0.866  1.276  5.687  8.164  13.429  23.705  43.523  83.680  164.031;
0.184 0.178 0.176 0.173 0.173 0.183 0.182 0.199 0.896 0.413 0.461 0.652 1.295  2.618  5.660 10.054  20.092  40.149  80.285 160.541  321.235;
0.144 0.112 0.113 0.112 0.113 0.122 0.124 0.151 0.649 0.401 0.735 1.384 2.706  5.405 10.066 20.084  40.150  80.194 160.566 320.871  641.942;
0.189 0.117 0.116 0.116 0.114 0.148 0.148 0.222 0.695 0.607 1.099 2.075 4.065  8.121 15.063 30.120  60.226 120.319 240.749 481.557  963.321;
0.220 0.131 0.131 0.131 0.132 0.197 0.198 0.295 0.799 0.819 1.465 2.770 5.425 10.826 20.090 40.160  80.293 160.440 321.125 642.033 1284.603;
0.261 0.165 0.165 0.203 0.165 0.243 0.246 0.369 0.917 1.016 1.830 3.454 6.788 13.522 25.110 50.196 100.377 200.562 401.159 802.801 1605.507;
0.295 0.198 0.198 0.198 0.192 0.296 0.296 0.443 1.066 1.219 2.191 4.139 8.138 16.204 30.127 60.234 120.437 240.709 481.583 963.180 1926.792;
];

put_mr_pure = 1000000 ./ put_mr_pure;

get_mr = [
 2.200  2.141  2.111  2.066  2.045  2.046  2.076  2.124  2.195  2.337  2.634  3.175  4.267  5.353  5.965  8.486  13.671  23.687  43.921  84.523  165.158;
 4.314  4.541  3.993  4.427  3.975  4.192  4.033  4.064  2.774  4.342  4.773  6.256  8.854  9.983 12.260 14.927  24.731  35.426  81.725 145.230  292.876;
 7.145  5.551  7.470  6.933  7.079  7.704  6.992  8.411  6.894  7.438  7.958  8.782 11.490 13.021 14.543 20.785  38.555  79.627 161.636 323.208  646.422;
10.283  7.677  7.795  7.892  7.844  8.286  8.128  8.324  8.265  9.381 10.002 14.257 16.831 25.985 28.221 33.556  58.483 116.710 241.232 483.847  970.347;
16.111 12.476 10.949 11.900 11.078 12.124 11.510 12.646 12.194 13.723 16.045 19.103 28.024 32.895 39.578 43.159  73.792 155.331 318.086 640.175 1290.004;
18.878 17.697 14.851 20.499 20.813 21.730 20.637 19.720 19.543 18.875 19.072 25.476 28.764 32.562 39.495 54.359  98.235 200.048 401.605 805.471 1616.568;
28.981 29.145 25.212 24.377 24.378 24.881 24.485 24.963 25.047 26.022 26.707 29.091 31.720 36.491 35.528 59.948 120.472 242.062 483.995 969.612 1938.986;
];

get_mr = 1000000 ./ get_mr;

get_mr_ctx = [
2.110 2.103 2.045 2.025 2.023 2.035 2.053 2.120 2.201 2.315 2.603 3.152 4.243  5.331  5.927  8.460  13.631  23.668  43.824  84.876  165.343;
2.306 2.241 2.196 2.160 2.150 2.130 2.140 2.190 2.243 2.373 2.660 3.208 4.305  5.326  6.055 10.056  20.076  40.101  80.287 160.523  321.181;
2.270 2.229 2.184 2.152 2.132 2.089 2.110 2.172 2.241 2.383 2.687 3.235 4.336  5.419 10.035 20.068  40.135  80.242 160.507 321.012  642.196;
2.269 2.228 2.193 2.164 2.115 2.094 2.122 2.182 2.275 2.404 2.681 3.214 4.312  7.536 15.050 30.101  60.201 120.341 240.669 481.568  963.274;
2.290 2.232 2.205 2.174 2.147 2.123 2.129 2.221 2.279 2.396 2.663 3.228 5.036 10.033 20.065 40.135  80.273 160.469 320.991 642.161 1284.485;
2.330 2.266 2.195 2.173 2.171 2.139 2.141 2.214 2.289 2.384 2.668 3.346 6.282 12.541 25.084 50.161 100.312 200.610 401.246 802.614 1605.552;
2.334 2.296 2.235 2.193 2.183 2.168 2.158 2.231 2.308 2.413 2.704 3.821 7.531 15.050 30.095 60.017 120.252 240.734 481.591 963.084 1923.773;
];

get_mr_ctx = 1000000 ./ get_mr_ctx;

get_mr_pure = [
2.202 2.218 2.098 2.063 2.054 2.049 2.065 2.137 2.216 2.351 2.640 3.186 4.301  5.348  5.998  8.487  13.653  23.709  44.166  84.332  165.155;
2.207 2.168 2.124 2.094 2.092 2.076 2.094 2.166 2.232 2.363 2.648 3.186 4.297  5.276  6.098 10.108  20.098  40.143  80.303 160.592  321.200;
2.165 2.152 2.107 2.087 2.083 2.114 2.120 2.197 2.269 2.416 2.671 3.220 4.289  5.362 10.047 20.071  40.137  80.251 160.505 320.882  642.081;
2.201 2.169 2.174 2.143 2.146 2.168 2.215 2.268 2.333 2.429 2.719 3.283 4.484  7.535 15.054 30.107  60.213 120.395 240.701 481.648  963.426;
2.102 2.113 2.128 2.084 2.081 2.107 2.152 2.183 2.231 2.365 2.673 3.255 5.036 10.035 20.072 40.141  80.278 160.536 320.965 641.906 1284.383;
2.114 2.099 2.125 2.135 2.101 2.109 2.148 2.218 2.253 2.374 2.699 3.388 6.276 12.540 25.081 50.174 100.345 200.537 401.276 802.665 1605.508;
2.126 2.133 2.113 2.125 2.129 2.123 2.147 2.219 2.263 2.409 2.735 3.819 7.527 15.051 30.097 60.199 120.409 240.683 481.562 963.306 1926.218;
];

get_mr_pure = 1000000 ./ get_mr_pure;


%% Compare all putmem/getmem
figure;

subplot(2, 3, 1);
% loglog(s, put_mr(1,:), '-^', s, put_mr(2,:), '-.s', s, put_mr(3,:), '--x', s, put_mr(4,:), ':d', s, put_mr(5,:), '-+', s, put_mr(6,:), '-.o', s, put_mr(7,:), ':*');
loglog(s, put_mr(1,:), '-d', s, put_mr(3,:), '--s', s, put_mr(5,:), '-.^', s, put_mr(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 1e7]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Thread Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 thread/node', '4 threads/node', '8 threads/node', '12 threads/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Single-context putmem', 'Interpreter', 'latex', 'fontsize', 14);

subplot(2, 3, 2);
% loglog(s, put_mr_ctx(1,:), '-^', s, put_mr_ctx(2,:), '-.s', s, put_mr_ctx(3,:), '--x', s, put_mr_ctx(4,:), ':d', s, put_mr_ctx(5,:), '-+', s, put_mr_ctx(6,:), '-.o', s, put_mr_ctx(7,:), ':*');
loglog(s, put_mr_ctx(1,:), '-d', s, put_mr_ctx(3,:), '--s', s, put_mr_ctx(5,:), '-.^', s, put_mr_ctx(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 1e7]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Thread Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 thread/node', '4 threads/node', '8 threads/node', '12 threads/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Multi-context putmem', 'Interpreter', 'latex', 'fontsize', 14);

subplot(2, 3, 3);
% loglog(s, put_mr_pure(1,:), '-^', s, put_mr_pure(2,:), '-.s', s, put_mr_pure(3,:), '--x', s, put_mr_pure(4,:), ':d', s, put_mr_pure(5,:), '-+', s, put_mr_pure(6,:), '-.o', s, put_mr_pure(7,:), ':*');
loglog(s, put_mr_pure(1,:), '-d', s, put_mr_pure(3,:), '--s', s, put_mr_pure(5,:), '-.^', s, put_mr_pure(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 1e7]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Process Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 process/node', '4 processes/node', '8 processes/node', '12 processes/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Single-threaded putmem', 'Interpreter', 'latex', 'fontsize', 14);

subplot(2, 3, 4);
% loglog(s, get_mr(1,:), '-^', s, get_mr(2,:), '-.s', s, get_mr(3,:), '--x', s, get_mr(4,:), ':d', s, get_mr(5,:), '-+', s, get_mr(6,:), '-.o', s, get_mr(7,:), ':*');
loglog(s, get_mr(1,:), '-d', s, get_mr(3,:), '--s', s, get_mr(5,:), '-.^', s, get_mr(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 7e5]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Thread Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 thread/node', '4 threads/node', '8 threads/node', '12 threads/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Single-context getmem', 'Interpreter', 'latex', 'fontsize', 14);

subplot(2, 3, 5);
% loglog(s, get_mr_ctx(1,:), '-^', s, get_mr_ctx(2,:), '-.s', s, get_mr_ctx(3,:), '--x', s, get_mr_ctx(4,:), ':d', s, get_mr_ctx(5,:), '-+', s, get_mr_ctx(6,:), '-.o', s, get_mr_ctx(7,:), ':*');
loglog(s, get_mr_ctx(1,:), '-d', s, get_mr_ctx(3,:), '--s', s, get_mr_ctx(5,:), '-.^', s, get_mr_ctx(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 7e5]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Thread Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 thread/node', '4 threads/node', '8 threads/node', '12 threads/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Multi-context getmem', 'Interpreter', 'latex', 'fontsize', 14);

subplot(2, 3, 6);
% loglog(s, get_mr_pure(1,:), '-^', s, get_mr_pure(2,:), '-.s', s, get_mr_pure(3,:), '--x', s, get_mr_pure(4,:), ':d', s, get_mr_pure(5,:), '-+', s, get_mr_pure(6,:), '-.o', s, get_mr_pure(7,:), ':*');
loglog(s, get_mr_pure(1,:), '-d', s, get_mr_pure(3,:), '--s', s, get_mr_pure(5,:), '-.^', s, get_mr_pure(7,:), ':o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([4e2 7e5]);
xticks(s);
xticklabels(sl);
xlabel('Message Size (Bytes)', 'Interpreter', 'latex', 'fontsize', 12);
ylabel('Process Message Rate (ops/s)', 'Interpreter', 'latex', 'fontsize', 12);
legend('1 process/node', '4 processes/node', '8 processes/node', '12 processes/node', 'Location', 'SouthWest', 'Interpreter', 'latex', 'fontsize', 12);
legend boxoff;
title('Single-threaded getmem', 'Interpreter', 'latex', 'fontsize', 14);
set(gcf, 'Position', [400, 300, 1764, 880]);
saveas(gcf, 'plot_putget', 'epsc');


%% AMOs
% 100000 iters of 32 bit atomic add
t = [1 2 4 6 8 10 12 14 16 18 20 22 24];
amo_post      = [0.846 1.257 3.567 4.656 6.769 7.612 10.495 12.459 14.919 16.831 17.486 20.627 22.611];
amo_post_ctx  = [0.750 1.301 2.420 3.168 4.222 6.069  6.136  7.534  8.773 10.003 11.050 12.291 13.639];
amo_post_pure = [0.776 1.357 2.092 3.162 4.189 5.448  6.479  7.144  8.898  9.171 10.535 11.964 12.598];

mr      = 1000000 ./ amo_post;
mr_ctx  = 1000000 ./ amo_post_ctx;
mr_pure = 1000000 ./ amo_post_pure;

figure;

subplot(1, 2, 1);
semilogy(t, mr, '--s', t, mr_ctx, '-.x', t, mr_pure, '-o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([1.4e4 1.5e6]);
xticks(t);
title('4 Byte Atomic Post Message Rate', 'Interpreter', 'latex');
xlabel('Number of Threads/Processes per Node', 'Interpreter', 'latex');
ylabel('Thread/Process Message Rate (ops/s)', 'Interpreter', 'latex');
legend('Single-context', 'Multi-context', 'Single-threaded', 'Interpreter', 'latex');
legend boxoff;


% 100000 iters of 32 bit atomic swap
t = [1 2 4 6 8 10 12 14 16 18 20 22 24];
amo_fetch      = [1.982 3.467 7.607 10.943 15.839 20.121 26.234 23.278 24.691 28.864 36.445 45.689 52.256];
amo_fetch_ctx  = [1.941 2.036 2.052  2.487  3.398  4.209  5.048  5.780  6.710  7.342  8.471  9.249 10.086];
amo_fetch_pure = [1.997 2.068 2.037  2.520  3.368  4.137  5.051  5.798  6.789  7.568  8.296  9.117 10.190];

mr      = 1000000 ./ amo_fetch;
mr_ctx  = 1000000 ./ amo_fetch_ctx;
mr_pure = 1000000 ./ amo_fetch_pure;

subplot(1, 2, 2);
semilogy(t, mr, '--s', t, mr_ctx, '-.x', t, mr_pure, '-o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([1.4e4 1.5e6]);
xticks(t);
title('4 Byte Atomic Fetch Message Rate', 'Interpreter', 'latex');
xlabel('Number of Threads/Processes per Node', 'Interpreter', 'latex');
ylabel('Thread/Process Message Rate (ops/s)', 'Interpreter', 'latex');
legend('Single-context', 'Multi-context', 'Single-threaded', 'Interpreter', 'latex');
legend boxoff;
set(gcf, 'Position', [800, 600, 1080, 450]);
saveas(gcf, 'plot_amo', 'epsc');


%% Ping-Pong
% 2^20 iters of 4 byte messages
t = [1 2 4 6 8 10 12 14 16 18 20 22 24];
pp      = [2.240 2.770 2.748 2.829 5.576 5.351 7.226 9.820 15.032 16.866 20.995 27.748 31.111];
pp_ctx  = [2.497 2.231 2.331 2.302 2.330 2.376 2.397 2.440  2.494  2.529  2.559  2.605  2.609];
pp_pure = [2.315 2.305 2.249 2.214 2.260 2.317 2.352 2.416  2.517  2.679  2.878  3.035  3.195];

figure;

subplot(1, 2, 1);
plot(t, pp, '--s', t, pp_ctx, '-.x', t, pp_pure, '-o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([0 90]);
xticks(t);
title('4 Byte Ping-Pong Latency (putmem)', 'Interpreter', 'latex');
xlabel('Number of Threads/Processes per Node', 'Interpreter', 'latex');
ylabel('Thread/Process Message Latency ($\mu$s)', 'Interpreter', 'latex');
legend('Single-context', 'Multi-context', 'Single-threaded', 'Location', 'NorthWest', 'Interpreter', 'latex');
legend boxoff;


% 2^20 iters of 4 byte messages
t = [1 2 4 6 8 10 12 14 16 18 20 22 24];
pp_amo      = [2.890 4.495 8.764 13.940 18.820 20.729 28.184 31.907 37.077 43.198 63.777 71.654 84.442];
pp_amo_ctx  = [2.910 2.991 3.117  3.311  3.600  4.417  5.272  6.645  7.920  9.204 10.797 11.853 13.166];
pp_amo_pure = [2.881 2.924 3.057  3.104  3.540  4.581  6.221  9.372 11.932 14.678 17.521 20.181 23.082];

subplot(1, 2, 2);
plot(t, pp_amo, '--s', t, pp_amo_ctx, '-.x', t, pp_amo_pure, '-o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
ylim([0 90]);
xticks(t);
title('4 Byte Ping-Pong Latency (atomics)', 'Interpreter', 'latex');
xlabel('Number of Threads/Processes per Node', 'Interpreter', 'latex');
ylabel('Thread/Process Message Latency ($\mu$s)', 'Interpreter', 'latex');
legend('Single-context', 'Multi-context', 'Single-threaded', 'Location', 'NorthWest', 'Interpreter', 'latex');
legend boxoff;
set(gcf, 'Position', [800, 600, 1080, 450]);
saveas(gcf, 'plot_pingpong', 'epsc');
