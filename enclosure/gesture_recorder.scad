// Gesture recorder enclosure: ESP32-C3 SuperMini (OLED variant), INMP441 round mic,
// GY-PCM5102 DAC, GY-521 IMU. Two parts, printed face down, M2 self-tapping screws.
//
//   openscad -D 'part="shell"' -o out/shell.stl gesture_recorder.scad
//   openscad -D 'part="lid"'   -o out/lid.stl   gesture_recorder.scad
//   openscad -D 'part="assembly"' ...            (preview with the modules in place)
//
// Every module dimension is a parameter below, with its source. The ones marked
// MEASURE are seller figures or estimates: put a caliper on your part before printing.

part = "assembly";        // [shell, lid, assembly, plate]

/* [Print] */
nozzle  = 0.4;
wall    = 2.0;            // 5 perimeters at 0.4
floor_t = 2.0;
lid_t   = 2.0;
tol     = 0.3;            // sliding clearance around boards
$fn     = 48;

/* [Case] */
in_l = 70.0;              // inner cavity, along X (jack end to USB end)
in_w = 56.0;              // inner cavity, along Y
in_h = 12.0;              // floor to lid underside
corner_r = 4.0;
post_d = 5.0;             // M2 self-tapping post
post_hole = 1.7;
lid_screw_d = 2.4;
lid_csk_d = 4.4;

/* [ESP32-C3 SuperMini with 0.42" OLED] */
// 25 x 20.5 mm, rows 17.78 mm apart (michiel.vanderwulp.be, OLED variant). Plain SuperMini: 22.52 x 18.
esp_l = 25.0;  esp_w = 20.5;  esp_t = 1.0;
esp_usb_w = 9.0;  esp_usb_h = 3.2;  esp_usb_over = 1.3;      // USB-C body 9 x 3.2, overhangs the edge 1.3 (lamina)
esp_oled_l = 12.0; esp_oled_w = 11.0; esp_oled_t = 1.2;      // bare 72x40 panel 12.0 x 11.0 x 1.2 (displaymodule.com)
esp_oled_active_l = 9.2; esp_oled_active_w = 5.2;            // active area 9.196 x 5.180
esp_oled_from_far = 2.0;   // MEASURE: panel edge distance from the end opposite the USB
esp_boot_xy = [4.5, 3.5];  // MEASURE: BOOT tact switch centre from the USB-end corner (x from USB edge, y from side)
esp_rst_xy  = [4.5, esp_w - 3.5];

/* [INMP441 round microphone] */
mic_d = 14.5;              // MEASURE: kit assumption, no manufacturer drawing found
mic_t = 1.2;
mic_pin_pitch = 2.0;       // six pads on a chord near one edge
mic_port_d = 1.0;          // through-PCB port under the MEMS can

/* [GY-PCM5102 DAC, purple] */
dac_l = 31.8;  dac_w = 23.7;  dac_t = 1.6;  dac_h = 6.35;   // MEASURE: seller listing (ebay); kit enclosure used 32 x 17
dac_jack_w = 6.0; dac_jack_h = 5.5; dac_jack_nose = 1.5;    // 3.5 mm socket body, nose past the PCB edge
dac_jack_off = 6.0;        // MEASURE: jack centre from the board's long edge, along the short (jack) edge
dac_plug_d = 8.8;          // wall hole for a 3.5 mm plug shroud

/* [GY-521 IMU] */
imu_l = 21.0;  imu_w = 16.0;  imu_t = 1.2;                   // 21 x 16 (protosupplies), 21.2 x 16.4 (grobotronics)
imu_hole_d = 3.0; imu_hole_from_end = 2.5; imu_hole_from_edge = 2.5;   // two holes, corners opposite the header (AZ-Delivery photo); ESTIMATE positions
imu_header_edge = "y0";    // 8-pin header along the y=0 long edge; chip X axis runs along imu_l

/* [Placement, inner cavity coordinates] */
dac_pos = [0.5, 7.5];                              // jack exits the x=0 wall; clear of the corner post
imu_pos = [39.0, 7.5];                             // long edge along X so pitch = nose up/down
esp_pos = [in_l - esp_l + esp_usb_over - 0.2, 27.5];   // USB-C exits the x=in_l wall
mic_pos = [18.0, in_w - 1.5 - mic_d / 2];          // under the lid grille, in the free area above the DAC
cradle_h = 3.0;
cradle_w = 1.6;

// ---------------------------------------------------------------------------
out_l = in_l + 2 * wall;  out_w = in_w + 2 * wall;
shell_h = floor_t + in_h;                          // lid sits on the wall top

module rbox(l, w, h, r) { linear_extrude(h) offset(r) offset(-r) square([l, w]); }

module posts(h) {
    for (p = [[corner_r + 0.5, corner_r + 0.5], [in_l - corner_r - 0.5, corner_r + 0.5],
              [corner_r + 0.5, in_w - corner_r - 0.5], [in_l - corner_r - 0.5, in_w - corner_r - 0.5]])
        translate([p[0], p[1], 0]) cylinder(d = post_d, h = h);
}
module post_holes(h) {
    for (p = [[corner_r + 0.5, corner_r + 0.5], [in_l - corner_r - 0.5, corner_r + 0.5],
              [corner_r + 0.5, in_w - corner_r - 0.5], [in_l - corner_r - 0.5, in_w - corner_r - 0.5]])
        translate([p[0], p[1], -1]) cylinder(d = post_hole, h = h + 2);
}

// A U-shaped cradle that holds a board by its edges, open on one side for the cable end.
module cradle(l, w, open = "none") {
    difference() {
        translate([-cradle_w - tol, -cradle_w - tol, 0]) cube([l + 2 * (cradle_w + tol), w + 2 * (cradle_w + tol), cradle_h]);
        translate([-tol, -tol, -1]) cube([l + 2 * tol, w + 2 * tol, cradle_h + 2]);
        if (open == "x0")   translate([-cradle_w - tol - 1, 2, -1]) cube([cradle_w + tol + 2, w - 4, cradle_h + 2]);
        if (open == "x1")   translate([l + tol - 1, 2, -1]) cube([cradle_w + tol + 2, w - 4, cradle_h + 2]);
    }
}

module shell() {
    difference() {
        union() {
            difference() {
                rbox(out_l, out_w, shell_h, corner_r + wall);
                translate([wall, wall, floor_t]) rbox(in_l, in_w, in_h + 1, corner_r);
            }
            translate([wall, wall, floor_t]) {
                posts(in_h);
                // DAC cradle, open toward the jack wall
                translate([dac_pos[0], dac_pos[1], 0]) cradle(dac_l, dac_w, "x0");
                // IMU cradle: sits flat on the floor, this is the tilt reference
                translate([imu_pos[0], imu_pos[1], 0]) cradle(imu_l, imu_w);
                // ESP cradle, open toward the USB wall
                translate([esp_pos[0], esp_pos[1], 0]) cradle(esp_l, esp_w, "x1");
            }
        }
        translate([wall, wall, 0]) post_holes(shell_h);
        // 3.5 mm jack: hole in the x=0 wall, centred on the socket
        translate([-1, wall + dac_pos[1] + dac_jack_off, floor_t + 1.2 + dac_t + dac_jack_h / 2])
            rotate([0, 90, 0]) cylinder(d = dac_plug_d, h = wall + 2);
        // USB-C: slot in the x=in_l wall
        translate([out_l - wall - 1, wall + esp_pos[1] + (esp_w - esp_usb_w) / 2 - 1.0, floor_t + 1.2 + esp_t - 0.5])
            cube([wall + 2, esp_usb_w + 2.0, esp_usb_h + 1.5]);
        // Cable shroud clearance outside the USB slot
        translate([out_l - 1.0, wall + esp_pos[1] + esp_w / 2, floor_t + 1.2 + esp_t + esp_usb_h / 2])
            rotate([0, 90, 0]) linear_extrude(2) offset(2) square([7.5 - 4, 13 - 4], center = true);
    }
}

module lid() {
    difference() {
        union() {
            rbox(out_l, out_w, lid_t, corner_r + wall);
            // mic collar under the lid: the round board drops in, port side up against the grille
            translate([wall + mic_pos[0], wall + mic_pos[1], -mic_t - 1.0])
                difference() {
                    cylinder(d = mic_d + 2 * tol + 2 * cradle_w, h = mic_t + 1.0);
                    translate([0, 0, -1]) cylinder(d = mic_d + 2 * tol, h = mic_t + 3);
                }
            // BOOT plunger guide
            translate([wall + esp_pos[0] + esp_l - esp_boot_xy[0], wall + esp_pos[1] + esp_boot_xy[1], -2.0])
                difference() { cylinder(d = 8, h = 2.0); translate([0, 0, -1]) cylinder(d = 5.4, h = 4); }
        }
        // screw holes, countersunk
        translate([wall, wall, 0]) for (p = [[corner_r + 0.5, corner_r + 0.5], [in_l - corner_r - 0.5, corner_r + 0.5],
                                             [corner_r + 0.5, in_w - corner_r - 0.5], [in_l - corner_r - 0.5, in_w - corner_r - 0.5]]) {
            translate([p[0], p[1], -1]) cylinder(d = lid_screw_d, h = lid_t + 2);
            translate([p[0], p[1], lid_t - 1.2]) cylinder(d1 = lid_screw_d, d2 = lid_csk_d, h = 1.21);
        }
        // mic grille: 7-hole rosette over the module centre
        translate([wall + mic_pos[0], wall + mic_pos[1], -1]) {
            cylinder(d = 2.0, h = lid_t + 2);
            for (a = [0 : 60 : 300]) rotate(a) translate([3.2, 0, 0]) cylinder(d = 1.6, h = lid_t + 2);
        }
        // OLED window over the panel's active area, with a 1 mm margin and a chamfer
        translate([wall + esp_pos[0] + esp_oled_from_far + esp_oled_l / 2, wall + esp_pos[1] + esp_w / 2, -1])
            hull() {
                translate([0, 0, 0]) linear_extrude(0.1) square([esp_oled_active_l + 2, esp_oled_active_w + 2], center = true);
                translate([0, 0, lid_t + 1]) linear_extrude(0.1) square([esp_oled_active_l + 4, esp_oled_active_w + 4], center = true);
            }
        // BOOT: hole for a printed plunger (or a fingertip)
        translate([wall + esp_pos[0] + esp_l - esp_boot_xy[0], wall + esp_pos[1] + esp_boot_xy[1], -3]) cylinder(d = 5.4, h = lid_t + 4);
        // RESET: pin hole
        translate([wall + esp_pos[0] + esp_l - esp_rst_xy[0], wall + esp_pos[1] + esp_rst_xy[1], -1]) cylinder(d = 1.6, h = lid_t + 2);
    }
}

// Printed plunger for the BOOT switch: cap above the lid, stem down to the tact switch.
module plunger() {
    stem = in_h - 1.2 - esp_t - 1.5;               // lid underside to the switch top (tact switch about 1.5 tall)
    cylinder(d = 5.0, h = stem + lid_t + 1.0);
    cylinder(d = 8.0, h = 1.0);                    // retaining flange under the lid guide
}

module modules() {
    color("mediumpurple") translate([wall + dac_pos[0], wall + dac_pos[1], floor_t + 1.2]) {
        cube([dac_l, dac_w, dac_t]);
        translate([-dac_jack_nose, dac_jack_off - dac_jack_w / 2, dac_t]) cube([12 + dac_jack_nose, dac_jack_w, dac_jack_h]);
    }
    color("steelblue") translate([wall + imu_pos[0], wall + imu_pos[1], floor_t + 1.2]) difference() {
        cube([imu_l, imu_w, imu_t]);
        for (x = [imu_hole_from_end, imu_l - imu_hole_from_end])
            translate([x, imu_w - imu_hole_from_edge, -1]) cylinder(d = imu_hole_d, h = 4);
    }
    color("darkslategray") translate([wall + esp_pos[0], wall + esp_pos[1], floor_t + 1.2]) {
        cube([esp_l, esp_w, esp_t]);
        translate([esp_l - 9 + esp_usb_over, (esp_w - esp_usb_w) / 2, esp_t]) cube([9, esp_usb_w, esp_usb_h]);
        color("white") translate([esp_oled_from_far, (esp_w - esp_oled_w) / 2, esp_t]) cube([esp_oled_l, esp_oled_w, esp_oled_t]);
        color("black") translate([esp_l - esp_boot_xy[0], esp_boot_xy[1], esp_t]) cylinder(d = 3, h = 1.5);
        color("black") translate([esp_l - esp_rst_xy[0], esp_rst_xy[1], esp_t]) cylinder(d = 3, h = 1.5);
    }
    color("darkgreen") translate([wall + mic_pos[0], wall + mic_pos[1], floor_t + in_h - mic_t]) difference() {
        cylinder(d = mic_d, h = mic_t);
        translate([0, 0, -1]) cylinder(d = mic_port_d, h = 4);
    }
}

if (part == "shell") shell();
if (part == "lid") { translate([0, 0, lid_t]) mirror([0, 0, 1]) lid(); }     // face down for printing
if (part == "plunger") plunger();
if (part == "assembly") { shell(); modules(); translate([0, 0, shell_h + 6]) lid(); }
if (part == "top") { shell(); modules(); }
if (part == "lidtop") lid();
if (part == "plate") { shell(); translate([out_l + 10, 0, lid_t]) mirror([0, 0, 1]) lid(); translate([-12, 10, 0]) plunger(); }
