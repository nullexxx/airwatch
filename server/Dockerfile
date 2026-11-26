FROM python:3.12-alpine
WORKDIR /app
COPY *.py ./
COPY static ./static
EXPOSE 8077
CMD ["python3", "-u", "receiver.py"]
