FROM python:3.12-alpine
WORKDIR /app
COPY *.py ./
COPY static ./static
EXPOSE 8077
# Runs as root on purpose: Docker creates the ./data bind mount owned by root,
# and a non-root user could not write uploads there without manual chown.
HEALTHCHECK --interval=60s --timeout=5s --start-period=20s \
  CMD wget -q -O /dev/null "http://127.0.0.1:${AIRWATCH_PORT:-8077}/healthz" || exit 1
CMD ["python3", "-u", "receiver.py"]
