import os.path

top = '.'
out = 'build'

def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    ctx.load('pebble_sdk')

def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for p in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[p])
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pebble_build_group(
            sources=ctx.path.ant_glob('src/**/*.c'),
            includes=['src'],
        )
        binaries.append({'platform': p, 'app_elf': app_elf})
        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            ctx.pebble_worker_build_group(
                sources=ctx.path.ant_glob('worker_src/**/*.c'),
                includes=['worker_src'],
            )
            binaries.append({'platform': p, 'app_elf': app_elf,
                             'worker_elf': worker_elf})
    ctx.set_env(cached_env)
    ctx.set_group('bundle')
    ctx.pebble_bundle(binaries=binaries,
                      js=ctx.path.ant_glob('src/pkjs/**/*.js'))
