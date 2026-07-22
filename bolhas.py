import random
import math

with open("bolhas.dat", "w") as f:
    for i in range(1024):
        # 1. Posição aleatória (esfera de raio 5)
        phi = random.uniform(0, 2 * math.pi)
        theta = math.acos(random.uniform(-1, 1))
        r = random.uniform(1, 4)
        
        cx = r * math.sin(theta) * math.cos(phi)
        cy = r * math.sin(theta) * math.sin(phi)
        cz = r * math.cos(theta)
        
        # 2. Vetor M: Apontando para o centro (Drift para aglutinar)
        # Ao apontar para dentro, forçamos o colapso inicial
        mx, my, mz = -cx, -cy, -cz
        norm = math.sqrt(mx**2 + my**2 + mz**2)
        mx, my, mz = mx/norm, my/norm, mz/norm
        
        # 3. Vetor S: Totalmente aleatório (quebra de simetria)
        sx = random.uniform(-1, 1)
        sy = random.uniform(-1, 1)
        sz = random.uniform(-1, 1)
        norm_s = math.sqrt(sx**2 + sy**2 + sz**2)
        sx, sy, sz = sx/norm_s, sy/norm_s, sz/norm_s
        
        # 4. Modo inicial (0 ou 1)
        modo = random.randint(0, 1)
        
        # Formato: cx cy cz raio mx my mz sx sy sz tipo modo_m
        f.write(f"{cx:.2f} {cy:.2f} {cz:.2f} 0.5 {mx:.2f} {my:.2f} {mz:.2f} {sx:.2f} {sy:.2f} {sz:.2f} 0 {modo}\n")