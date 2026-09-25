// The C3 case in its assembled positions, one STL per colour group (STL has no colours).
include <../../enclosure/samplooler.scad>
sel = "shell";
if (sel == "shell") shell();
if (sel == "lid") translate([0, 0, shell_h]) lid();
if (sel == "dac") translate([wall + dac_pos[0], wall + dac_pos[1], floor_t + 1.2]) {
    cube([dac_l, dac_w, dac_t]);
    translate([-dac_jack_nose, dac_jack_off - dac_jack_w / 2, dac_t]) cube([12 + dac_jack_nose, dac_jack_w, dac_jack_h]);
}
if (sel == "imu") translate([wall + imu_pos[0], wall + imu_pos[1], floor_t + 1.2]) difference() {
    cube([imu_l, imu_w, imu_t]);
    for (x = [imu_hole_from_end, imu_l - imu_hole_from_end])
        translate([x, imu_w - imu_hole_from_edge, -1]) cylinder(d = imu_hole_d, h = 4);
}
if (sel == "esp") translate([wall + esp_pos[0], wall + esp_pos[1], floor_t + 1.2]) {
    cube([esp_l, esp_w, esp_t]);
    translate([esp_l - 9 + esp_usb_over, (esp_w - esp_usb_w) / 2, esp_t]) cube([9, esp_usb_w, esp_usb_h]);
}
if (sel == "oled") translate([wall + esp_pos[0], wall + esp_pos[1], floor_t + 1.2])
    translate([esp_oled_from_far, (esp_w - esp_oled_w) / 2, esp_t]) cube([esp_oled_l, esp_oled_w, esp_oled_t]);
if (sel == "buttons") translate([wall + esp_pos[0], wall + esp_pos[1], floor_t + 1.2]) {
    translate([esp_l - esp_boot_xy[0], esp_boot_xy[1], esp_t]) cylinder(d = 3, h = 1.5, $fn = 24);
    translate([esp_l - esp_rst_xy[0], esp_rst_xy[1], esp_t]) cylinder(d = 3, h = 1.5, $fn = 24);
}
if (sel == "mic") translate([wall + mic_pos[0], wall + mic_pos[1], floor_t + in_h - mic_t]) difference() {
    cylinder(d = mic_d, h = mic_t, $fn = 48);
    translate([0, 0, -1]) cylinder(d = mic_port_d, h = 4);
}
if (sel == "plunger") translate([wall + esp_pos[0] + esp_l - esp_boot_xy[0], wall + esp_pos[1] + esp_boot_xy[1],
                                 floor_t + 1.2 + esp_t + 1.5]) plunger();
