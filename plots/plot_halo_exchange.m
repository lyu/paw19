clear;clc;


n         = [2 3 4 6 8 12 16 24 32];

halo      = [14.4101 16.849  16.3348 14.5634  13.0408  11.0424  9.86976 8.89299 8.01355] .* 2;
halo_ctx  = [12.1568 12.0456 12.0045  9.90089  8.92711  7.03746 6.02975 5.13845 4.56046] .* 2;
halo_pure = [13.5779 13.5565 13.1091 12.4395  10.979    9.7496  9.35707 8.27818 7.81917] .* 2;

figure;
semilogx(n, halo, '--s', n, halo_ctx, '-x', n, halo_pure, '-.o', 'LineWidth', 1);
pbaspect([1.2 1 1]);
xlim([2 32]);
xticks(n);
xlabel('Number of Nodes', 'Interpreter', 'latex');
ylabel('Iteration Time (msec)', 'Interpreter', 'latex');
legend('Single-context', 'Multi-context', 'Pure SHMEM', 'Location', 'SouthWest', 'Interpreter', 'latex');
legend boxoff;
title('3D Halo Exchange', 'Interpreter', 'latex');
set(gcf, 'Position', [800, 600, 540, 450]);
saveas(gcf, 'plot_halo', 'epsc');
