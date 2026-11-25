"""dataout_listen.py: receive FM3's Forza "Data Out" telemetry (FM3_DATA_OUT, src/hooks/data_out.cpp) and print or
record it (2026-11-25).

    python -I tools/analysis/dataout_listen.py [--port 5300] [--seconds 20] [--csv out.csv] [--horizon]

Prints one decoded line a second and, at the end, how many packets came in per second and the ranges seen.
"""
import argparse
import socket
import struct
import time

SLED = [('IsRaceOn', 'i'), ('TimestampMS', 'I'), ('EngineMaxRpm', 'f'), ('EngineIdleRpm', 'f'),
        ('CurrentEngineRpm', 'f'), ('AccelX', 'f'), ('AccelY', 'f'), ('AccelZ', 'f'), ('VelX', 'f'), ('VelY', 'f'),
        ('VelZ', 'f'), ('AngVelX', 'f'), ('AngVelY', 'f'), ('AngVelZ', 'f'), ('Yaw', 'f'), ('Pitch', 'f'),
        ('Roll', 'f')] + \
       [('NormSusp' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('SlipRatio' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('WheelRot' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('Rumble' + w, 'i') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('Puddle' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('SurfRumble' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('SlipAngle' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('CombSlip' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('SuspM' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('CarOrdinal', 'i'), ('CarClass', 'i'), ('CarPI', 'i'), ('Drivetrain', 'i'), ('Cylinders', 'i')]
DASH = [('PosX', 'f'), ('PosY', 'f'), ('PosZ', 'f'), ('Speed', 'f'), ('Power', 'f'), ('Torque', 'f')] + \
       [('TyreTemp' + w, 'f') for w in ('FL', 'FR', 'RL', 'RR')] + \
       [('Boost', 'f'), ('Fuel', 'f'), ('Distance', 'f'), ('BestLap', 'f'), ('LastLap', 'f'), ('CurrentLap', 'f'),
        ('RaceTime', 'f'), ('Lap', 'H'), ('Position', 'B'), ('Accel', 'B'), ('Brake', 'B'), ('Clutch', 'B'),
        ('HandBrake', 'B'), ('Gear', 'B'), ('Steer', 'b'), ('DrivingLine', 'b'), ('AIBrake', 'b')]


def decode(data, horizon):
    sled_fmt = '<' + ''.join(t for _, t in SLED)
    dash_fmt = '<' + ''.join(t for _, t in DASH)
    sled = struct.unpack_from(sled_fmt, data, 0)
    dash = struct.unpack_from(dash_fmt, data, 232 + (12 if horizon else 0))
    return dict(zip([n for n, _ in SLED], sled)) | dict(zip([n for n, _ in DASH], dash))


def main():
    a = argparse.ArgumentParser()
    a.add_argument('--port', type=int, default=5300)
    a.add_argument('--seconds', type=float, default=20)
    a.add_argument('--csv')
    a.add_argument('--horizon', action='store_true')
    o = a.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', o.port))
    s.settimeout(0.5)
    rows, sizes = [], set()
    t0 = time.time()
    last_print = 0
    while time.time() - t0 < o.seconds:
        try:
            data, _ = s.recvfrom(2048)
        except socket.timeout:
            continue
        sizes.add(len(data))
        d = decode(data, o.horizon)
        d['_t'] = time.time() - t0
        rows.append(d)
        if d['_t'] - last_print >= 1.0:
            last_print = d['_t']
            print('%5.1fs on %d rpm %5.0f/%5.0f gear %2d speed %5.1f vel %+.1f %+.1f %+.1f acc %+.1f %+.1f %+.1f '
                  'angv %+.2f %+.2f %+.2f yaw %+.2f thr %3d brk %3d steer %+4d tyres %.0f/%.0f/%.0f/%.0f F '
                  'susp %.2f %.2f %.2f %.2f' % (
                      d['_t'], d['IsRaceOn'], d['CurrentEngineRpm'], d['EngineMaxRpm'], d['Gear'], d['Speed'],
                      d['VelX'], d['VelY'], d['VelZ'], d['AccelX'], d['AccelY'], d['AccelZ'], d['AngVelX'],
                      d['AngVelY'], d['AngVelZ'], d['Yaw'], d['Accel'], d['Brake'], d['Steer'], d['TyreTempFL'],
                      d['TyreTempFR'], d['TyreTempRL'], d['TyreTempRR'], d['NormSuspFL'], d['NormSuspFR'],
                      d['NormSuspRL'], d['NormSuspRR']))
    print('%d packets in %.0f s (%.1f a second), sizes %s' % (len(rows), o.seconds, len(rows) / o.seconds, sorted(sizes)))
    if o.csv and rows:
        keys = list(rows[0])
        with open(o.csv, 'w', newline='\n') as f:
            f.write(','.join(keys) + '\n')
            for r in rows:
                f.write(','.join(str(r[k]) for k in keys) + '\n')


if __name__ == '__main__':
    main()
